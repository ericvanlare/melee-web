// SPDX-License-Identifier: GPL-3.0-or-later
// One immutable PRE, two independently verified late-input corrections, then
// checked retirement/rebase. This is a diagnostic, not live match admission.
import {correctedPageDefaults,recordPredictedActionDivergence} from './cpu_corrected_runner.mjs';
import {collectCpuEndpointInPage,decodeCpuParticleEndpointResult,
  compareCpuParticleEndpoints,compareCpuReferenceParticleEndpoints,
  validateCpuParticleEndpointAgainstPre} from './cpu_endpoint.mjs';
import {decodeDrawDiagnostics,validateAuroraFrame,validateSelectedDrawPath,
  validateSourceInputMapping} from './runner_contract.mjs';
import {compareCpuCorrectedRawFive} from './cpu_corrected_two_window_runner.mjs';
import {decodeCpuHistoryMotorEvidence,
  compareCpuCorrectedLoopMotorEvents,decodeCpuCorrectedLoopMotorCommitReceipt}
  from './cpu_corrected_loop_runner.mjs';
import {buildCpuHistoryCorpus,createCpuHistoryTimeline,admitCpuHistoryRows,
  planCpuHistoryReplay} from './cpu_history_input.mjs';

export const CPU_HISTORY_COMMAND=Object.freeze({capture:64,begin:65,forward:66,draw:67,
  idle:68,seal:69,archive:70,restore:71,replay:72,finish:73,close:74,retire:75});
const fields=['schema','bytes','phase','lastCommand','firstFailure','preFrame','currentFrame',
  'sealedEnd','incarnation','restoreCount','captures','retirements','activeCapsules',
  'maxActiveCapsules','forwardSteps','replaySteps','drawSubmissions','sourceStepNoDrawMask',
  'commitResult','reserved'];
const decode=value=>Buffer.from(value,'base64');

export function requireCpuHistoryMotor(value,{preFrame,span,selectedLane,baseline=null}){
  // Endpoint frames use the uint64 decimal ABI; motor journals use uint32.
  // Join only canonical, exactly representable frame values across those ABIs.
  if(typeof preFrame!=='string'||!/^(0|[1-9][0-9]*)$/.test(preFrame)||
     BigInt(preFrame)>0xffffffffn)
    throw new Error('History endpoint PRE frame is outside the motor ABI');
  const motorPreFrame=Number(preFrame);
  if(value?.mode!==2||value.failure!==0||value.commitState!==0||value.published!==0||value.selectedLane!==selectedLane)
    throw new Error('History motor capture-only owner refused');
  const a=value.baseline,b=value.selected;
  if(a.state!==1||a.framesSeen!==a.span||a.span<1||a.span>span||
     b.state!==1||b.framesSeen!==span||b.span!==span||a.firstFrame!==motorPreFrame||b.firstFrame!==motorPreFrame)
    throw new Error('History motor selected prefix extent drift');
  for(const key of ['token','world','contextEpoch'])
    if(a[key]==='0'||a[key]!==b[key])throw new Error(`History motor ${key} owner drift`);
  if(baseline&&JSON.stringify(a)!==JSON.stringify(baseline))
    throw new Error('History motor original journal changed after continuation');
  if(selectedLane===1&&JSON.stringify(a)!==JSON.stringify(b))
    throw new Error('History first forward motor journal differs');
  return value;
}

export function decodeCpuHistoryMetadata(input){
  const bytes=Buffer.from(input);
  if(bytes.length!==80)throw new Error('History metadata extent');
  const value=Object.fromEntries(fields.map((field,index)=>[field,bytes.readUInt32LE(index*4)]));
  if(value.schema!==1||value.bytes!==80||value.phase>7||value.reserved!==0||
     value.firstFailure!==0||value.incarnation>3||value.restoreCount>2||
     value.activeCapsules>1||value.maxActiveCapsules>1||
     value.captures-value.retirements!==value.activeCapsules||
     value.currentFrame<value.preFrame||value.currentFrame-value.preFrame>7||
     value.sourceStepNoDrawMask>3)
    throw new Error(`History metadata owner/bound violation: ${JSON.stringify(value)}`);
  return value;
}

export async function retainCpuHistoryFailureAnchor(report,current,sinkAttempted,save,error){
  if(!current)return;
  current.status='failed';current.error=String(error);
  // Keep the full raw anchor even when its durable sink was the failing step.
  // A failed sink is never retried, and failure grants no retirement authority.
  report.partialAnchor=current;
  if(!sinkAttempted){
    try{await save();}catch(saveError){report.evidenceError=String(saveError);}
  }
}

// One native command and immutable byte copies. PAD is staged only at an
// explicit source tick; an endpoint batch has its own synchronous collector.
export async function operateCpuHistoryInPage(plan){
  const {module}=window.__slippiSynctest;
  const progress=window.__slippiSynctest.cpuHistoryProgress??={status:'running',commands:[]};
  const entry={command:plan.command,status:'pending'};progress.commands.push(entry);
  const createTimer=new Function(`return (${plan.commandTimingSource})`)();
  const timer=createTimer();
  const encode=bytes=>{let text='';for(let i=0;i<bytes.length;i+=0x6000)
    text+=String.fromCharCode(...bytes.subarray(i,i+0x6000));return btoa(text);};
  const read=(pointer,size)=>{
    if(!Number.isSafeInteger(pointer)||pointer<=0||!Number.isSafeInteger(size)||size<=0||
       pointer+size>module.HEAPU8.length)throw new Error('History borrowed byte extent');
    return module.HEAPU8.slice(pointer,pointer+size);
  };
  const metadata=()=>{
    if(module._melee_web_synctest_cpu_history_evidence_bytes()!==80)
      throw new Error('History native metadata ABI changed');
    return encode(read(module._melee_web_synctest_cpu_history_evidence(),80));
  };
  try{
    if(plan.enable){
      const result=module._melee_web_synctest_cpu_history_enable(1);
      if(result!==0)throw new Error(`History enable refused ${result}`);
    }
    if(plan.wire){
      const wire=new Uint8Array(plan.wire.length*16),view=new DataView(wire.buffer);
      plan.wire.forEach((row,i)=>row.forEach((value,j)=>view.setUint32(i*16+j*4,value,true)));
      const pointer=module._malloc(wire.length);if(!pointer)throw new Error('History static wire allocation failed');
      try{
        module.HEAPU8.set(wire,pointer);
        const result=module._melee_web_synctest_cpu_restore_configure(pointer,plan.wire.length);
        if(result!==0)throw new Error(`History static wire refused ${result}`);
      }finally{module._free(pointer);}
    }
    if(plan.pad!==undefined){
      if(plan.command!==66&&plan.command!==72)throw new Error('History PAD staging outside source tick');
      const pad=Uint8Array.from(atob(plan.pad),value=>value.charCodeAt(0));
      if(pad.length!==44)throw new Error('History PAD ABI changed');
      const pointer=module._melee_web_snapshot_input();read(pointer,44);module.HEAPU8.set(pad,pointer);
    }
    timer.beginNativeCall();
    entry.result=await module.ccall('melee_web_synctest_cpu_history_command','number',
      ['number'],[plan.command],{async:true});
    timer.endNativeCall();
    entry.metadataBase64=metadata();
    if(entry.result!==0)throw new Error(`History command ${plan.command} refused ${entry.result}: `+
      module.UTF8ToString(module._melee_web_snapshot_error()));
    let gpu=null;
    if(plan.draw){
      const wait=new Function(`return (${plan.waitSource})`)();timer.beginGpuWait();
      gpu=await wait(module,plan.command);timer.endGpuWait();
    }
    const raw={};
    for(const [name,pointer,size] of [
      ['observation',module._melee_web_snapshot_observation(),module._melee_web_snapshot_observation_size()],
      ['pcm',module._melee_web_snapshot_pcm(),module._melee_web_snapshot_pcm_size()],
      ['pad',module._melee_web_snapshot_input(),44],
      ['rngProfile',module._melee_web_snapshot_rng_profile_observation(),module._melee_web_snapshot_rng_profile_observation_size()],
      ['transfer',module._melee_web_snapshot_transfer_diagnostic(),module._melee_web_snapshot_transfer_diagnostic_size()]]){
      if(size!==({observation:1280,pcm:4272,pad:44,rngProfile:32,transfer:40})[name])
        throw new Error(`History ${name} ABI changed`);
      raw[name]=encode(read(pointer,size));
    }
    if(module._melee_web_synctest_draw_diagnostics_size()!==132)throw new Error('History draw diagnostics ABI changed');
    const draw=read(module._melee_web_synctest_draw_diagnostics(),132);
    const decodeDraw=new Function(`return (${plan.decodeDrawSource})`)();
    const validateMapping=new Function(`return (${plan.inputMappingSource})`)();
    const inspect=new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)(
      {interactive:0,entry:1,death:2,respawn:3,unknown:4},validateMapping);
    const boundary=inspect(read(module._melee_web_snapshot_observation(),1280),{
      classifyMotion:value=>module._melee_web_synctest_motion_boundary(value),
      isMarioKind:value=>module._melee_web_synctest_is_mario_kind(value)===1,
      slotCount:decodeDraw(draw).slotCount,expectedInputMapping:plan.mapping,expectedStocks:plan.stocks});
    if(!boundary.interactive)throw new Error(`History interactive boundary refused: ${JSON.stringify(boundary.refusalReasons)}`);
    let motor=null;
    if(plan.motor){
      const suffix=plan.command===75?'commit_receipt':'evidence';
      const prefix='_melee_web_synctest_cpu_history_motor_'+suffix;
      const size=module[prefix+'_bytes']();
      if(size!==(plan.command===75?64:1016))throw new Error('History motor ABI changed');
      motor=encode(read(module[prefix](),size));
    }
    entry.status='complete';entry.timing=timer.finish();
    return {metadataBase64:entry.metadataBase64,raw,drawDiagnosticsBase64:encode(draw),
      boundary,gpu,motorBase64:motor,timing:entry.timing};
  }catch(error){
    entry.status='failed';entry.error=String(error);entry.timing=timer.fail();progress.status='failed';
    try{entry.metadataBase64=metadata();}catch(readError){entry.metadataError=String(readError);}
    throw error;
  }
}

export async function runBrowserCpuHistory(predictedPage,referencePage,wire,checkpoint,
  expectedInputMapping,stocks,previousFrame,beforeDiagnostics,onEvidence,{packetInput}={}){
  if(!packetInput||typeof onEvidence!=='function')
    throw new Error('History witness requires actual received relay bytes and retained evidence sink');
  const mapping=validateSourceInputMapping(expectedInputMapping,beforeDiagnostics.slotCount);
  if(mapping[0].rawController!==0||mapping[1].rawController!==1||stocks.length!==2||stocks.some(x=>x!==4))
    throw new Error('History witness requires four-stock source ports0/1');
  const sourceStart=checkpoint.boundary.sourceFrame;
  if(!Number.isSafeInteger(sourceStart)||sourceStart<0||!decode(checkpoint.pad).subarray(11,22).every(x=>x===0))
    throw new Error('History initial source frame/remote seed differs');
  const corpus=buildCpuHistoryCorpus(),timeline=createCpuHistoryTimeline(),received=new Map();
  const defaults=correctedPageDefaults(mapping,stocks),gpu=new Map(),diagnostics=new Map();
  const report={schema:'browser-retained-pre-history-v1',status:'running',
    scope:'one private native PRE, two actual local relay arrivals, source-only correction and confirmed rebase',
    sourceStart,anchors:[],comparisons:[],timeline:[],commands:{reference:[],predicted:[]}};
  for(const page of [referencePage,predictedPage]){gpu.set(page,previousFrame);diagnostics.set(page,beforeDiagnostics);}
  let current=null,sinkAttempted=false,selected=[],referenceRaw=[],anchorSample=300;
  const lane=page=>page===referencePage?'reference':'predicted';
  const operate=async(page,command,options={})=>{
    const before=diagnostics.get(page);
    const result=await page.evaluate(operateCpuHistoryInPage,{...defaults,...options,command});
    const metadata=decodeCpuHistoryMetadata(decode(result.metadataBase64));
    if(metadata.lastCommand!==command||metadata.currentFrame!==result.boundary.sourceFrame)
      throw new Error('History command/source cursor metadata drift');
    const draw=decodeDrawDiagnostics(decode(result.drawDiagnosticsBase64));
    if(command===66||command===72){
      if(JSON.stringify(draw)!==JSON.stringify(before))throw new Error('History source-only command ran draw work');
      if(options.pad!==result.raw.pad)throw new Error('History consumed PAD differs from literal planned input');
    }
    if(options.draw){
      const prior=gpu.get(page),next=validateAuroraFrame(result.gpu,prior.beginFrameId,prior.endFrameId);
      validateSelectedDrawPath(before,draw,result.boundary.players,mapping);gpu.set(page,next);
    }
    diagnostics.set(page,draw);
    const record={command,metadata,timing:result.timing,
      ...(result.motorBase64?{motorBase64:result.motorBase64}:{})};report.commands[lane(page)].push(record);
    current?.commands.push({lane:lane(page),...record});
    return {...result,metadata};
  };
  const endpoint=async(page,view,provenance)=>decodeCpuParticleEndpointResult(
    await page.evaluate(collectCpuEndpointInPage,{view,provenance,particleCellEndpoint:true}));
  const draw=async(page)=>{await operate(page,67,{draw:true});return operate(page,68);};
  const requireFrame=(result,sample)=>{
    if(result.metadata.currentFrame!==sourceStart+sample-299)throw new Error(`History source cursor differs at sample ${sample}`);
  };
  const save=async()=>{
    sinkAttempted=true;const receipt=await onEvidence(report.anchors.length-1,current);
    if(!receipt||typeof receipt.sha256!=='string'||!/^[0-9a-f]{64}$/.test(receipt.sha256))
      throw new Error('History evidence sink did not return a hash receipt');
    report.anchors.at(-1).evidence=receipt;
  };
  try{
    for(let anchor=0;anchor<2;anchor++){
      anchorSample=anchor===0?300:303;selected=[];referenceRaw=[];
      current={anchor,anchorSample,status:'running',commands:[],captured:{},armed:{},incarnations:[],comparisons:[],motorInitial:{}};
      report.anchors.push({anchor,anchorSample,status:'running'});sinkAttempted=false;
      for(const page of [referencePage,predictedPage]){
        const captured=await operate(page,64,{wire,enable:anchor===0});
        if(captured.metadata.preFrame!==sourceStart+anchorSample-300||captured.metadata.captures!==anchor+1||
           captured.metadata.retirements!==anchor||captured.metadata.activeCapsules!==1||captured.metadata.restoreCount!==0)
          throw new Error('History capture/rebase identity drift');
        current.captured[lane(page)]=await endpoint(page,1,1);
        await operate(page,65);current.armed[lane(page)]=await endpoint(page,1,1);
      }
      current.comparisons.push(compareCpuReferenceParticleEndpoints(current.captured.reference,current.captured.predicted));
      current.comparisons.push(compareCpuReferenceParticleEndpoints(current.armed.reference,current.armed.predicted));
      for(const [index,batch] of (anchor===0?[[300,2],[302,1]]:[[303,1]]).entries()){
        const [start,span]=batch,correct=anchor===0;
        const spec={startSample:start,span,rows:corpus.slice(start-300,start-300+span),kind:correct?'speculative-history':'confirmed-rebase'};
        const incarnation={index,start,span,referenceRows:[],predictedRows:[],replayRows:[],comparisons:[],endpoints:{}};
        current.incarnations.push(incarnation);packetInput.prepareCapsule(spec);
        if(!correct){
          await packetInput.publishCapsule(spec);incarnation.packet=await packetInput.drainCapsule(spec);
          admitCpuHistoryRows(timeline,received,spec.rows.map((_,i)=>({tick:start+i,remotePad:packetInput.getRemotePad(start+i)})));
        }
        if(index){await operate(referencePage,65);await operate(predictedPage,65);}
        for(let offset=0;offset<span;offset++){
          const tick=start+offset,literal=decode(corpus[tick-300]);
          timeline.recordLocal(tick,literal.subarray(0,11));
          const plan=timeline.planForward();
          if(plan.kind!=='forward'||plan.tick!==tick)throw new Error('History next forward plan refused');
          const reference=await operate(referencePage,66,{pad:literal.toString('base64')});
          const predicted=await operate(predictedPage,66,{pad:Buffer.from(plan.frame).toString('base64')});
          requireFrame(reference,tick);requireFrame(predicted,tick);
          timeline.commitForward(plan);selected.push({tick,frame:Buffer.from(plan.frame)});
          referenceRaw.push(reference.raw);incarnation.referenceRows.push(reference);incarnation.predictedRows.push(predicted);
          if(!correct)incarnation.comparisons.push(compareCpuCorrectedRawFive(reference.raw,predicted.raw,`confirmed ${tick}`));
          if(offset+1<span){await draw(referencePage);await draw(predictedPage);}
        }
        const refSeal=await operate(referencePage,69,{motor:true}),predSeal=await operate(predictedPage,69,{motor:true});
        const refCpu=await endpoint(referencePage,2,2),predCpu=await endpoint(predictedPage,2,2);
        incarnation.endpoints.referenceCpu=refCpu;incarnation.endpoints.predictedCpu=predCpu;
        validateCpuParticleEndpointAgainstPre(refCpu,current.armed.reference);
        validateCpuParticleEndpointAgainstPre(predCpu,current.armed.predicted);
        const refDraw=await draw(referencePage);
        incarnation.finalPresents={reference:refDraw};
        if(correct){
          const requested=process.hrtime.bigint().toString();
          const submitted=operate(predictedPage,67,{draw:true}).then(result=>{
            packetInput.notePredictedDrawResolved(start);return result;
          });
          const results=await Promise.allSettled([submitted,packetInput.publishCapsule(spec,{predictedDrawRequestNs:requested})]);
          for(const result of results)if(result.status==='rejected')throw result.reason;
          incarnation.finalPresents.predicted=await operate(predictedPage,68);
        }else{
          const predDraw=await draw(predictedPage);
          incarnation.finalPresents.predicted=predDraw;
          incarnation.comparisons.push(compareCpuCorrectedRawFive(refDraw.raw,predDraw.raw,
            'confirmed history rebase final present'));
        }
        const refDrawEnd=await endpoint(referencePage,3,2),predDrawEnd=await endpoint(predictedPage,3,2);
        incarnation.endpoints.referenceDraw=refDrawEnd;incarnation.endpoints.predictedDraw=predDrawEnd;
        let selectedMotor=decodeCpuHistoryMotorEvidence(decode(predSeal.motorBase64));
        const referenceMotor=decodeCpuHistoryMotorEvidence(decode(refSeal.motorBase64));
        const total=start+span-anchorSample;
        requireCpuHistoryMotor(referenceMotor,{preFrame:current.armed.reference.sourceFrame,span:total,
          selectedLane:index===0?1:3,baseline:current.motorInitial.reference});
        requireCpuHistoryMotor(selectedMotor,{preFrame:current.armed.predicted.sourceFrame,span:total,
          selectedLane:index===0?1:3,baseline:current.motorInitial.predicted});
        if(index===0){current.motorInitial.reference=referenceMotor.baseline;current.motorInitial.predicted=selectedMotor.baseline;}
        if(correct){
          incarnation.packet=await packetInput.drainCapsule(spec);
          admitCpuHistoryRows(timeline,received,spec.rows.map((_,i)=>({tick:start+i,remotePad:packetInput.getRemotePad(start+i)})));
          const replay=planCpuHistoryReplay(timeline,selected,received,anchorSample);
          incarnation.replay={from:replay.dirtyFrom,through:replay.through,
            frames:replay.frames.map(row=>({tick:row.tick,pad:row.frame.toString('base64'),unchangedPrefix:row.unchangedPrefix}))};
          incarnation.divergence=recordPredictedActionDivergence(refCpu,predCpu);
          if(!incarnation.referenceRows.some((row,i)=>row.boundary.players[1].motion!==incarnation.predictedRows[i].boundary.players[1].motion))
            throw new Error('History late input did not change actual remote motion');
          await operate(predictedPage,70);await operate(predictedPage,71);
          const restored=await endpoint(predictedPage,1,1);incarnation.endpoints.restoredPre=restored;
          incarnation.comparisons.push(compareCpuReferenceParticleEndpoints(current.armed.predicted,restored));
          for(const [i,row] of replay.frames.entries()){
            const actual=await operate(predictedPage,72,{pad:row.frame.toString('base64')});requireFrame(actual,row.tick);
            incarnation.replayRows.push(actual);
            incarnation.comparisons.push(compareCpuCorrectedRawFive(referenceRaw[i],actual.raw,`history correction ${index}/${row.tick}`));
          }
          const finished=await operate(predictedPage,73,{motor:true});
          if(finished.metadata.restoreCount!==index+1||finished.metadata.captures!==1)
            throw new Error('History correction did not retain the original PRE');
          const correctedCpu=await endpoint(predictedPage,2,3);incarnation.endpoints.correctedCpu=correctedCpu;
          incarnation.comparisons.push(compareCpuParticleEndpoints(refCpu,correctedCpu,{preLeft:current.armed.reference,preRight:current.armed.predicted}));
          const correctedDraw=await draw(predictedPage),correctedDrawEnd=await endpoint(predictedPage,3,3);
          incarnation.finalPresents.corrected=correctedDraw;
          incarnation.endpoints.correctedDraw=correctedDrawEnd;
          incarnation.comparisons.push(compareCpuParticleEndpoints(refDrawEnd,correctedDrawEnd,{preLeft:current.armed.reference,preRight:current.armed.predicted}));
          incarnation.comparisons.push(compareCpuCorrectedRawFive(refDraw.raw,correctedDraw.raw,`history final present ${index}`));
          selectedMotor=decodeCpuHistoryMotorEvidence(decode(finished.motorBase64));
          requireCpuHistoryMotor(selectedMotor,{preFrame:current.armed.predicted.sourceFrame,span:total,selectedLane:3,
            baseline:current.motorInitial.predicted});
          timeline.commitReplay(replay.replay);selected=replay.frames.map(row=>({tick:row.tick,frame:Buffer.from(row.frame)}));
        }else{
          const options={preLeft:current.armed.reference,preRight:current.armed.predicted};
          incarnation.comparisons.push(compareCpuReferenceParticleEndpoints(refCpu,predCpu,options));
          incarnation.comparisons.push(compareCpuReferenceParticleEndpoints(refDrawEnd,predDrawEnd,options));
          requireCpuHistoryMotor(selectedMotor,{preFrame:current.armed.predicted.sourceFrame,span:total,selectedLane:1});
        }
        incarnation.motor={reference:referenceMotor,selected:selectedMotor,
          comparison:compareCpuCorrectedLoopMotorEvents(referenceMotor.selected,selectedMotor.selected)};
        await operate(referencePage,74);await operate(predictedPage,74);
        report.timeline.push(timeline.status());
      }
      current.status='compared';await save();
      const last=current.incarnations.at(-1).motor;
      for(const page of [referencePage,predictedPage]){
        const result=await operate(page,75,{motor:true}),motor=page===referencePage?last.reference:last.selected;
        const receipt=decodeCpuCorrectedLoopMotorCommitReceipt(decode(result.motorBase64),motor.selected,motor.selectedLane);
        if(result.metadata.retirements!==anchor+1||result.metadata.activeCapsules!==0||result.metadata.maxActiveCapsules!==1)
          throw new Error('History confirmed retirement failed');
        (report.anchors.at(-1).retirement??={})[lane(page)]={metadata:result.metadata,motor:receipt};
      }
      report.anchors.at(-1).status='complete';current=null;
    }
    const status=timeline.status();
    if(status.confirmed_through!==303||status.simulated_through!==303||status.dirty_from!==null||status.plan_outstanding)
      throw new Error('History witness did not finish confirmed at sample303');
    report.status='passed';report.finalTimeline=status;return report;
  }catch(error){
    report.status='failed';report.error=String(error);
    report.nativeProgress={};
    for(const page of [referencePage,predictedPage]){
      try{report.nativeProgress[lane(page)]=await page.evaluate(()=>window.__slippiSynctest?.cpuHistoryProgress??null);}
      catch(readError){report.nativeProgress[lane(page)]={readError:String(readError)};}
    }
    await retainCpuHistoryFailureAnchor(report,current,sinkAttempted,save,error);
    error.cpuHistoryReport=report;throw error;
  }
}
