// SPDX-License-Identifier: GPL-3.0-or-later
// One actual wrong-input branch corrected against an independent drawn source.
import {
  CPU_SEVEN_TICK_PAD44_CORPUS, CPU_SEVEN_TICK_INITIAL_REMOTE_PAD,
  createCpuSevenTickTimeline, planCpuSevenTickForward, acknowledgeCpuSevenTickForward,
  finishCpuSevenTickPrediction, planCpuSevenTickCorrection, decodeCpuSevenTickEvidence,
} from './cpu_seven_tick_runner.mjs';
import {waitForAuroraSubmission} from './cpu_restore_runner.mjs';
import {collectCpuEndpointInPage,decodeCpuEndpointResult,compareCpuEndpoints} from './cpu_endpoint.mjs';
import {validateSourceInputMapping,decodeDrawDiagnostics,validateAuroraFrame,
  validateSelectedDrawPath,inspectInteractiveObservation} from './runner_contract.mjs';

const bytes=value=>Buffer.from(value,'base64');
const same=(a,b)=>bytes(a).equals(bytes(b));
const expectedPhase=command=>command===32?1:command<=38?2:command===39?3:
  command===40?4:command===41?5:command<=48?6:command===49?7:
  command===50?8:command===51?9:command===52?10:0;

export function validateCorrectedCommandEvidence(evidence, command, sourceFrame) {
  if(!evidence||evidence.schema!==2||evidence.mode!==7||evidence.lastCommand!==command||
     evidence.firstFailure!==0||evidence.state!==expectedPhase(command)||
     evidence.preFrame!==sourceFrame||evidence.postFrame!==sourceFrame+7||
     evidence.cpuEquality!==0||evidence.drawnEquality!==0||evidence.checkedCpu!==0||
     evidence.matchedCpu!==0||evidence.checkedDrawn!==0||evidence.matchedDrawn!==0)
    throw new Error(`Corrected local command ${command} phase/frame/evidence drift`);
  if(command>=42&&command<=51&&evidence.commitResult!==0)
    throw new Error(`Corrected command ${command} lacks successful local restore`);
  if(command>=39&&evidence.baselineStepNoDrawMask!==127)
    throw new Error('Seven baseline source-only steps were not observed');
  if(command>=49&&command<=51&&evidence.replayStepNoDrawMask!==127)
    throw new Error('Seven corrective no-draw steps were not observed');
  return evidence;
}

export function compareCorrectedRawPairs(reference, corrected) {
  const lanes=['Observation','Pcm','Pad','RngProfile','Transfer'];
  if(reference.tickPairs?.length!==7||corrected.tickPairs?.length!==7)
    throw new Error('Independent raw comparison requires seven exact pairs');
  for(let i=0;i<7;++i){
    const a=reference.tickPairs[i],b=corrected.tickPairs[i];
    if(a.preFrame!==b.preFrame||a.postFrame!==b.postFrame)
      throw new Error(`Corrected raw source frame differs at sample ${300+i}`);
    for(const lane of lanes){
      const left=bytes(a[`baseline${lane}`]),right=bytes(b[`replay${lane}`]);
      if(!left.equals(right)){
        const offset=left.length!==right.length?'extent':left.findIndex((x,j)=>x!==right[j]);
        throw new Error(`Corrected raw ${lane} differs at sample ${300+i}, byte ${offset}`);
      }
    }
  }
  return {equal:true,samples:7,lanes};
}

export function recordPredictedActionDivergence(reference, predicted) {
  const selections=endpoint=>{
    const actions=endpoint.domains.find(domain=>domain.id===3);
    if(!actions)throw new Error('Missing ActionEvent domain for wrong-branch proof');
    return actions.records.filter(record=>record.kindName==='ActionEvent'&&
      record.fields[record.fieldNames.indexOf('Kind')]==='2').map(record=>({
        index:record.index,fields:record.fields,fieldNames:record.fieldNames,bytesBase64:record.bytesBase64}));
  };
  const a=selections(reference),b=selections(predicted);
  // Require a different actual selection request, not merely an allocation or
  // cache-hit difference. Full event keys remain retained in both endpoints.
  const requests=rows=>rows.map(row=>['Destination','Motion','Slot','ResultDomain'].map(
    name=>row.fields[row.fieldNames.indexOf(name)]));
  if(!a.length&&!b.length||JSON.stringify(requests(a))===JSON.stringify(requests(b)))
    throw new Error('Wrong branch did not change actual action-selection requests');
  return {different:true,reference:a,predicted:b,
    scope:'actual selection request divergence; remote-player motion divergence is observed separately'};
}

// This callback performs source work only for the explicitly issued command.
// Endpoint reads have their own synchronous callback with no await or source work.
export function correctedPageDefaults(mapping,stocks){
  return {mapping,stocks,waitSource:waitForAuroraSubmission.toString(),
    decodeDrawSource:decodeDrawDiagnostics.toString(),inputMappingSource:validateSourceInputMapping.toString(),
    inspectSource:inspectInteractiveObservation.toString()};
}

export async function operateCorrectedInPage(plan){
  const {module}=window.__slippiSynctest;
  const progress=window.__slippiSynctest.cpuCorrectedProgress??=
    {status:'running',commands:[],nativeEvidenceBase64:null};
  const entry={command:plan.command,outcome:'pending'};progress.commands.push(entry);
  const encode=value=>{let text='';for(let i=0;i<value.length;i+=0x6000)
    text+=String.fromCharCode(...value.subarray(i,i+0x6000));return btoa(text);};
  const read=(pointer,size)=>{
    if(!Number.isSafeInteger(pointer)||pointer<=0||!Number.isSafeInteger(size)||size<=0||
       pointer+size>module.HEAPU8.length)throw new Error('Corrected native byte extent');
    return module.HEAPU8.slice(pointer,pointer+size);
  };
  const readEvidence=()=>{
    if(module._melee_web_synctest_cpu_corrected_evidence_bytes()!==93512)
      throw new Error('Corrected evidence size drift');
    return encode(read(module._melee_web_synctest_cpu_corrected_evidence(),93512));
  };
  const readReuseEvidence=()=>{
    if(module._melee_web_synctest_cpu_corrected_reuse_evidence_bytes()!==40)
      throw new Error('Corrected reuse evidence size drift');
    return encode(read(module._melee_web_synctest_cpu_corrected_reuse_evidence(),40));
  };
  try{
    if(typeof SharedArrayBuffer!=='undefined'&&module.HEAPU8.buffer instanceof SharedArrayBuffer)
      throw new Error('Corrected route refuses shared Wasm memory');
    if(plan.reuseEnable){
      if(typeof module._melee_web_synctest_cpu_corrected_reuse_enable!=='function')
        throw new Error('Corrected reuse enable export is unavailable');
      const result=module._melee_web_synctest_cpu_corrected_reuse_enable();
      progress.reuseEnableResult=result;if(result!==0)throw new Error(`Corrected reuse enable refused ${result}`);
    }
    if(plan.wire){
      const wire=new Uint8Array(plan.wire.length*16),view=new DataView(wire.buffer);
      plan.wire.forEach((row,i)=>row.forEach((value,j)=>view.setUint32(i*16+j*4,value,true)));
      const pointer=module._malloc(wire.length);if(!pointer)throw new Error('CPU wire allocation failed');
      try{
        module.HEAPU8.set(wire,pointer);
        const result=module._melee_web_synctest_cpu_restore_configure(pointer,plan.wire.length);
        progress.configurationResult=result;if(result!==0)throw new Error(`CPU configuration refused ${result}`);
      }finally{module._free(pointer);}
    }
    if(plan.pad!==undefined){
      const pad=Uint8Array.from(atob(plan.pad),value=>value.charCodeAt(0));
      if(pad.length!==44)
        throw new Error('Corrected PAD ABI drift');
      const pointer=module._melee_web_snapshot_input();read(pointer,44);module.HEAPU8.set(pad,pointer);
    }
    entry.result=await module.ccall('melee_web_synctest_cpu_corrected_command','number',
      ['number'],[plan.command],{async:true});
    entry.outcome='returned';
    if(entry.result!==0)throw new Error(`Corrected command ${plan.command} refused ${entry.result}: `+
      module.UTF8ToString(module._melee_web_snapshot_error()));
    if(plan.command===53){
      entry.reuseEvidenceBase64=readReuseEvidence();
      progress.reuseEvidenceBase64=entry.reuseEvidenceBase64;
      return {reuseEvidenceBase64:entry.reuseEvidenceBase64};
    }
    progress.nativeEvidenceBase64=readEvidence();
    if(plan.readReuseEvidence){
      entry.reuseEvidenceBase64=readReuseEvidence();
      progress.reuseEvidenceBase64=entry.reuseEvidenceBase64;
    }
    let status=null;
    if(plan.draw){
      const wait=new Function(`return (${plan.waitSource})`)();status=await wait(module,plan.command);
    }
    const observation=read(module._melee_web_snapshot_observation(),1280);
    const draw=read(module._melee_web_synctest_draw_diagnostics(),132);
    const decodeDraw=new Function(`return (${plan.decodeDrawSource})`)();
    const validateMapping=new Function(`return (${plan.inputMappingSource})`)();
    const inspect=new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)(
      {interactive:0,entry:1,death:2,respawn:3,unknown:4},validateMapping);
    const boundary=inspect(observation,{classifyMotion:value=>module._melee_web_synctest_motion_boundary(value),
      isMarioKind:value=>module._melee_web_synctest_is_mario_kind(value)===1,
      slotCount:decodeDraw(draw).slotCount,expectedInputMapping:plan.mapping,expectedStocks:plan.stocks});
    if(!boundary.interactive)throw new Error(`Corrected route lost interactive boundary: ${JSON.stringify(boundary.refusalReasons)}`);
    return {evidenceBase64:progress.nativeEvidenceBase64,drawDiagnosticsBase64:encode(draw),boundary,status,
      reuseEvidenceBase64:entry.reuseEvidenceBase64??null};
  }catch(error){
    entry.error=String(error);progress.status='failed';
    try{progress.nativeEvidenceBase64=readEvidence();}catch(readError){progress.readError=String(readError);}
    throw error;
  }
}

export async function runBrowserCpuCorrectedSeven(predictedPage,referencePage,wire,checkpoint,
  expectedInputMapping,stocks,previousFrame,beforeDiagnostics){
  const mapping=validateSourceInputMapping(expectedInputMapping,beforeDiagnostics.slotCount);
  const localPort=mapping[0].rawController,remotePort=mapping[1].rawController;
  if(!bytes(checkpoint.pad).subarray(remotePort*11,(remotePort+1)*11).equals(bytes(CPU_SEVEN_TICK_INITIAL_REMOTE_PAD)))
    throw new Error('Corrected route initial remote PAD differs from checkpoint');
  const pre=checkpoint.boundary.sourceFrame;
  const schedule=createCpuSevenTickTimeline(localPort,remotePort,CPU_SEVEN_TICK_INITIAL_REMOTE_PAD);
  const report={schema:'independent-browser-correction-v1',status:'running',sourceFrame:pre,
    sourceSteps:7,reference:{commands:[],rows:[],endpoints:{}},predicted:{commands:[],rows:[],endpoints:{}},
    comparisons:{},scope:'one private Mario/FD correction; no rolling snapshots, live relay or profile acceptance'};
  const defaults={mapping,stocks,waitSource:waitForAuroraSubmission.toString(),
    decodeDrawSource:decodeDrawDiagnostics.toString(),inputMappingSource:validateSourceInputMapping.toString(),
    inspectSource:inspectInteractiveObservation.toString()};
  const stateFor=(page)=>page===referencePage?report.reference:report.predicted;
  const gpu=new Map([[referencePage,previousFrame],[predictedPage,previousFrame]]);
  const diagnostics=new Map([[referencePage,beforeDiagnostics],[predictedPage,beforeDiagnostics]]);
  const operate=async(page,command,options={})=>{
    const result=await page.evaluate(operateCorrectedInPage,{...defaults,...options,command});
    const evidence=decodeCpuSevenTickEvidence(bytes(result.evidenceBase64),2);
    validateCorrectedCommandEvidence(evidence,command,pre);
    const state=stateFor(page);state.nativeEvidence=evidence;state.commands.push({command,result:0,state:evidence.state});
    if(options.draw){
      const old=gpu.get(page),current=validateAuroraFrame(result.status,old.beginFrameId,old.endFrameId);
      const draw=decodeDrawDiagnostics(bytes(result.drawDiagnosticsBase64));
      const selected=validateSelectedDrawPath(diagnostics.get(page),draw,result.boundary.players,mapping);
      state.draws??=[];state.draws.push({command,gpu:result.status,selected});
      gpu.set(page,current);diagnostics.set(page,draw);
    }
    return {result,evidence};
  };
  const endpoint=async(page,view,provenance,name)=>{
    const result=decodeCpuEndpointResult(await page.evaluate(collectCpuEndpointInPage,{view,provenance}));
    stateFor(page).endpoints[name]=result;return result;
  };
  try{
    // Native setup order and allocations are identical before PRE comparison.
    await operate(referencePage,32,{wire});await operate(predictedPage,32,{wire});
    const refPre=await endpoint(referencePage,1,1,'pre');
    const predPre=await endpoint(predictedPage,1,1,'pre');
    report.comparisons.pre=compareCpuEndpoints(refPre,predPre);
    for(let i=0;i<7;++i){
      const literal=CPU_SEVEN_TICK_PAD44_CORPUS[i];
      const planned=planCpuSevenTickForward(schedule,300+i,literal);
      const predicted=Buffer.from(planned.frame).toString('base64');
      const ref=await operate(referencePage,33+i,{pad:literal,draw:i<6});
      const pred=await operate(predictedPage,33+i,{pad:predicted,draw:i<6});
      const refPair=ref.evidence.tickPairs[i],predPair=pred.evidence.tickPairs[i];
      if(!same(refPair.baselinePad,literal)||!same(predPair.baselinePad,predicted)||
         ref.result.boundary.sourceFrame!==pre+i+1||pred.result.boundary.sourceFrame!==pre+i+1)
        throw new Error(`Native consumed input/source frame differs at sample ${300+i}`);
      acknowledgeCpuSevenTickForward(schedule,planned);
      report.reference.rows.push({sample:300+i,pad44Base64:literal,boundary:ref.result.boundary});
      report.predicted.rows.push({sample:300+i,pad44Base64:predicted,boundary:pred.result.boundary,timelineAcknowledged:true});
    }
    report.predictionGuard=finishCpuSevenTickPrediction(schedule);
    if(!report.reference.rows.some((row,i)=>row.boundary.players[1].motion!==report.predicted.rows[i].boundary.players[1].motion))
      throw new Error('Wrong-input interval did not demonstrate a different remote action');
    const refCpu=await endpoint(referencePage,2,2,'cpu');
    // Native Reference means this capsule's recorded tape. On this page it is
    // speculative evidence only and is never the expected corrected endpoint.
    const predictedCpu=await endpoint(predictedPage,2,2,'speculativeCpu');
    report.actionDivergence=recordPredictedActionDivergence(refCpu,predictedCpu);
    await operate(referencePage,40,{draw:true});await operate(referencePage,41);
    await endpoint(referencePage,3,2,'draw');
    await operate(predictedPage,40,{draw:true});await operate(predictedPage,41);
    const correction=planCpuSevenTickCorrection(schedule.timeline,report.reference.rows,localPort,remotePort);
    report.correction={earliestChanged:correction.earliestChanged,frames:correction.frames.map(x=>({tick:x.tick,frame:Buffer.from(x.frame).toString('base64')}))};
    await operate(predictedPage,42);
    for(let i=0;i<7;++i)await operate(predictedPage,43+i,{pad:Buffer.from(correction.frames[i].frame).toString('base64')});
    const correctedCpu=await endpoint(predictedPage,2,3,'cpu');
    report.comparisons.cpu=compareCpuEndpoints(report.reference.endpoints.cpu,correctedCpu);
    report.comparisons.raw=compareCorrectedRawPairs(report.reference.nativeEvidence,report.predicted.nativeEvidence);
    // Acknowledge correction only after all native execution and host comparison.
    schedule.timeline.commitReplay(correction.replay);
    report.timeline=schedule.timeline.status();
    await operate(predictedPage,50,{draw:true});await operate(predictedPage,51);
    const correctedDraw=await endpoint(predictedPage,3,3,'draw');
    report.comparisons.draw=compareCpuEndpoints(report.reference.endpoints.draw,correctedDraw);
    const a=report.reference.nativeEvidence.drawnPair,b=report.predicted.nativeEvidence.drawnPair;
    for(const lane of ['Observation','Pcm','Pad','RngProfile','Transfer'])
      if(!same(a[`baseline${lane}`],b[`replay${lane}`]))throw new Error(`Final drawn raw ${lane} differs`);
    await operate(referencePage,52);await operate(predictedPage,52);
    report.status='passed';return report;
  }catch(error){
    report.status='failed';report.failure=String(error);
    for(const [name,page] of [['reference',referencePage],['predicted',predictedPage]]){
      try{report[name].partial=await page.evaluate(()=>window.__slippiSynctest?.cpuCorrectedProgress??null);}
      catch(readError){report[name].partialReadError=String(readError);}
    }
    error.cpuCorrectedPartial=report;throw error;
  }
}
