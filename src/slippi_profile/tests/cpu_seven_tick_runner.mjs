// SPDX-License-Identifier: GPL-3.0-or-later
// Private fixed seven-tick browser witness. The timeline owns inputs only;
// the second page's reviewed typed CPU capsule is the sole source restore.
import {MatchInputTimeline, ROLLBACK_MAX_PREDICTION_TICKS} from '../../../scripts/net_rollback_input_timeline.mjs';
import {NET_FRAME_BYTES, PAD_BYTES} from '../../../scripts/net_lockstep_core.mjs';
import {waitForAuroraSubmission} from './cpu_restore_runner.mjs';
import {
  decodeDrawDiagnostics, inspectInteractiveObservation, validateAuroraFrame,
  validateSelectedDrawPath, validateSourceInputMapping,
} from './runner_contract.mjs';

export const CPU_SEVEN_TICK_EVIDENCE_BYTES = 93512;
export const CPU_SEVEN_TICK_PAIR_BYTES = 11416;
export const CPU_SEVEN_TICK_DIAGNOSTIC_BYTES = 132;
export const CPU_SEVEN_TICK_SAMPLES = Object.freeze([300, 301, 302, 303, 304, 305, 306]);
export const CPU_SEVEN_TICK_INITIAL_REMOTE_PAD = 'AAAAAAAAAAAAAAA=';
// This zero PAD exists only to ask planForward about tick 307 after seven
// acknowledged ticks. It is never passed to the native source step.
export const CPU_SEVEN_TICK_GUARD_LOCAL_PAD = 'AAAAAAAAAAAAAAA=';
export const CPU_SEVEN_TICK_PAD44_CORPUS = Object.freeze([
  'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  'AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
]);

if (NET_FRAME_BYTES !== 44 || PAD_BYTES !== 11 || ROLLBACK_MAX_PREDICTION_TICKS !== 7)
  throw new Error('Seven-tick browser runner requires the fixed four-port PAD and seven-tick timeline contract');

const encode = bytes => Buffer.from(bytes).toString('base64');
const decode = value => Buffer.from(value, 'base64');
const padSlot = (pad, port) => pad.subarray(port * PAD_BYTES, (port + 1) * PAD_BYTES);

export function createCpuSevenTickTimeline(localPort, remotePort, initialRemotePad) {
  if(initialRemotePad===undefined)throw new Error('Seven-tick initial remote PAD must be supplied explicitly');
  const seed = typeof initialRemotePad === 'string' ? decode(initialRemotePad) : Buffer.from(initialRemotePad);
  if (seed.length !== PAD_BYTES) throw new Error('Seven-tick initial remote PAD must be an explicit 11-byte value');
  const timeline = new MatchInputTimeline({localPort, remotePort, startTick: 300,
    initialRemotePad: seed, maxHistoryTicks: 8, maxLookaheadTicks: 8});
  return {timeline, localPort, remotePort, initialRemotePad:Buffer.from(seed), frames:[], guard:null};
}

export function planCpuSevenTickForward(schedule,tick,pad44) {
  const index=CPU_SEVEN_TICK_SAMPLES.indexOf(tick);
  if(!schedule||index<0||schedule.frames.length!==index||schedule.pendingPlan)
    throw new Error(`Seven-tick forward request is out of order at ${tick}`);
  const literal=decode(pad44),expected=decode(CPU_SEVEN_TICK_PAD44_CORPUS[index]);
  if(literal.length!==NET_FRAME_BYTES||!literal.equals(expected))
    throw new Error(`Seven-tick literal PAD frame drift at ${tick}`);
  schedule.timeline.recordLocal(tick,padSlot(literal,schedule.localPort));
  const plan=schedule.timeline.planForward();
  const expectedPrediction=Buffer.from(literal);
  expectedPrediction.set(schedule.initialRemotePad,schedule.remotePort*PAD_BYTES);
  const plannedFrame=Buffer.from(plan.frame);
  if(plan.kind!=='forward'||plan.tick!==tick||plan.remote_source!=='last_known_prediction'||
     plan.speculative_ticks!==index+1||plannedFrame.length!==NET_FRAME_BYTES||!plannedFrame.equals(expectedPrediction)||
     !padSlot(plannedFrame,schedule.localPort).equals(padSlot(literal,schedule.localPort))||
     !padSlot(plannedFrame,schedule.remotePort).equals(schedule.initialRemotePad))
    throw new Error(`Seven-tick predicted input plan drift at ${tick}`);
  schedule.pendingPlan=plan;
  return plan;
}

export function acknowledgeCpuSevenTickForward(schedule,plan) {
  if(!schedule||!plan||schedule.pendingPlan!==plan||schedule.frames.length>=7)
    throw new Error('Seven-tick forward acknowledgement is stale or out of order');
  schedule.timeline.commitForward(plan);
  const frame=Buffer.from(plan.frame);
  schedule.frames.push({tick:plan.tick,frame,localPad:Buffer.from(padSlot(frame,schedule.localPort)),
    remotePad:Buffer.from(padSlot(frame,schedule.remotePort)),speculativeTicks:plan.speculative_ticks});
  schedule.pendingPlan=null;
}

export function finishCpuSevenTickPrediction(schedule) {
  if(!schedule||schedule.pendingPlan||schedule.frames.length!==7)
    throw new Error('Seven-tick prediction ended before seven executed and acknowledged ticks');
  if(schedule.timeline.status().simulated_through!==306)
    throw new Error('Seven-tick guard requires the native-acknowledged cursor at 306');
  const guardLocal=decode(CPU_SEVEN_TICK_GUARD_LOCAL_PAD);
  schedule.timeline.recordLocal(307,guardLocal);
  const plan = schedule.timeline.planForward();
  const after=schedule.timeline.status();
  if (plan.status !== 'blocked' || plan.reason !== 'prediction_limit' || plan.tick !== 307 ||
      after.simulated_through!==306||after.plan_outstanding) {
    throw new Error(`Seven-tick timeline did not block the eighth prediction: ${JSON.stringify(plan)}`);
  }
  const guard={...plan,localPadBase64:encode(guardLocal),nativeStepExecuted:false};
  schedule.guard=guard;
  return guard;
}

export function planCpuSevenTickCorrection(timeline, baselineRows, localPort, remotePort) {
  if (!timeline || !Array.isArray(baselineRows) || baselineRows.length !== 7)
    throw new Error('Seven authoritative source rows are required before remote correction');
  let earliestChanged = null;
  const remoteRows = [];
  for (let index = 0; index < 7; ++index) {
    const row = baselineRows[index];
    const tick = CPU_SEVEN_TICK_SAMPLES[index];
    const full = decode(row?.pad44Base64);
    const expected = decode(CPU_SEVEN_TICK_PAD44_CORPUS[index]);
    if (row?.sample !== tick || full.length !== NET_FRAME_BYTES || !full.equals(expected))
      throw new Error(`Authoritative literal 44-byte PAD drift at sample ${tick}`);
    const remotePad = padSlot(full, remotePort);
    const accepted = timeline.acceptRemote(tick, remotePad);
    if (accepted.status !== 'accepted') throw new Error(`Remote PAD ${tick} was refused: ${JSON.stringify(accepted)}`);
    if (earliestChanged === null && accepted.earliest_changed_tick !== null)
      earliestChanged = accepted.earliest_changed_tick;
    remoteRows.push({tick, pad: Buffer.from(remotePad)});
  }
  const state = timeline.status();
  if (earliestChanged !== 300 || state.dirty_from !== 300 || state.remote_contiguous_through !== 306)
    throw new Error(`Seven-tick remote delivery did not invalidate the complete suffix: ${JSON.stringify({earliestChanged,state})}`);
  const replay = timeline.planReplay();
  if (replay.kind !== 'replay' || replay.from !== 300 || replay.through !== 306 ||
      replay.frames.length !== 7 || replay.speculative_ticks !== 0)
    throw new Error(`Correction plan is not exactly the seven-tick suffix: ${JSON.stringify(replay)}`);
  const frames = replay.frames.map((frame, index) => {
    const actual = decode(CPU_SEVEN_TICK_PAD44_CORPUS[index]);
    if (frame.tick !== 300 + index || !Buffer.from(frame.local_pad).equals(padSlot(actual, localPort)) ||
        !Buffer.from(frame.remote_pad).equals(padSlot(actual, remotePort)) || !Buffer.from(frame.frame).equals(actual))
      throw new Error(`Correction plan changed authoritative/local bytes at sample ${300 + index}`);
    return {tick: frame.tick, frame: Buffer.from(frame.frame), localPad: Buffer.from(frame.local_pad),
      remotePad: Buffer.from(frame.remote_pad)};
  });
  return {earliestChanged, dirtyFrom: state.dirty_from,
    remoteContiguousThrough: state.remote_contiguous_through, replay, frames, remoteRows};
}

export function decodeCpuSevenTickEvidence(bytes, expectedSchema = 1) {
  const size = CPU_SEVEN_TICK_EVIDENCE_BYTES, pairSize = CPU_SEVEN_TICK_PAIR_BYTES;
  const diagnosticStride = CPU_SEVEN_TICK_DIAGNOSTIC_BYTES;
  const diagnosticOffset = 91400;
  if (!(bytes instanceof Uint8Array) || bytes.byteLength !== size)
    throw new Error('Seven-tick evidence extent');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const u32 = offset => view.getUint32(offset, true);
  if (![1,2].includes(expectedSchema) || u32(0) !== expectedSchema || u32(4) !== size || u32(8) !== 7)
    throw new Error('Seven-tick evidence schema/mode');
  const b64 = value => encode(value);
  const decodePair = offset => {
    const raw = bytes.slice(offset, offset + pairSize), row = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
    if (row.getUint32(0, true) !== 1 || row.getUint32(4, true) !== pairSize)
      throw new Error('Seven-tick raw-pair schema/bytes');
    const read = (start, count) => b64(raw.slice(start, start + count));
    const comparison = Object.fromEntries(['domain','status','row','offset','expected','actual','address','extent','detail']
      .map((name, index) => [name, row.getUint32(11376 + index * 4, true)]));
    const result = {bytes: pairSize, schema: row.getUint32(0, true), declaredBytes: row.getUint32(4, true),
      state: row.getUint32(8, true), firstFailure: row.getUint32(12, true), preFrame: row.getUint32(16, true),
      postFrame: row.getUint32(20, true), commitResult: row.getUint32(24, true), equality: row.getUint32(28, true),
      baselineObservation: read(32, 1280), replayObservation: read(1312, 1280),
      baselinePcm: read(2592, 4272), replayPcm: read(6864, 4272),
      baselinePad: read(11136, 44), replayPad: read(11180, 44),
      baselineRngProfile: read(11224, 32), replayRngProfile: read(11256, 32),
      baselineTransfer: read(11288, 40), replayTransfer: read(11328, 40),
      checkedDomains: row.getUint32(11368, true), matchedDomains: row.getUint32(11372, true), comparison};
    result.rawFiveMatch = ['baselineObservation','baselinePcm','baselinePad','baselineRngProfile','baselineTransfer']
      .every((name, index) => result[name] === result[
        ['replayObservation','replayPcm','replayPad','replayRngProfile','replayTransfer'][index]]);
    result.base64 = b64(raw);
    return result;
  };
  const diagnosticMask = u32(56);
  if ((diagnosticMask & ~0xffff) !== 0) throw new Error('Seven-tick diagnostic mask contains unknown slots');
  const diagnostics = Array.from({length:16}, (_, index) => {
    if (!(diagnosticMask & (1 << index))) return null;
    const raw = bytes.slice(diagnosticOffset + index * diagnosticStride,
      diagnosticOffset + (index + 1) * diagnosticStride);
    return {index, bytes: diagnosticStride, base64: b64(raw)};
  });
  return {schema:u32(0), bytes:u32(4), mode:u32(8), state:u32(12), firstFailure:u32(16),
    preFrame:u32(20), postFrame:u32(24), commitResult:u32(28), cpuEquality:u32(32), drawnEquality:u32(36),
    checkedCpu:u32(40), matchedCpu:u32(44), checkedDrawn:u32(48), matchedDrawn:u32(52), diagnosticMask,
    lastCommand:u32(60), baselineStepNoDrawMask:u32(64), replayStepNoDrawMask:u32(68),
    tickPairs:Array.from({length:7}, (_, index) => decodePair(72 + index * pairSize)),
    drawnPair:decodePair(79984), diagnostics, base64:b64(bytes)};
}

export function validateCpuSevenTickFinalEvidence(evidence, checkpointSourceFrame) {
  const fail = message => { throw new Error(`Seven-tick ${message}`); };
  const pairSize = CPU_SEVEN_TICK_PAIR_BYTES;
  if (!evidence || evidence.schema !== 1 || evidence.bytes !== CPU_SEVEN_TICK_EVIDENCE_BYTES ||
      evidence.mode !== 7 || evidence.state !== 5 || evidence.firstFailure !== 0 || evidence.lastCommand !== 31 ||
      evidence.commitResult !== 0 || evidence.cpuEquality !== 1 || evidence.drawnEquality !== 1 ||
      evidence.checkedCpu !== 0x3fe || evidence.matchedCpu !== 0x3fe || evidence.checkedDrawn !== 0x3fe ||
      evidence.matchedDrawn !== 0x3fe || evidence.diagnosticMask !== 0xffff ||
      evidence.baselineStepNoDrawMask !== 0x7f || evidence.replayStepNoDrawMask !== 0x7f ||
      evidence.preFrame !== checkpointSourceFrame || evidence.postFrame !== checkpointSourceFrame + 7)
    fail('final header/typed endpoint contract missing');
  if (!Array.isArray(evidence.tickPairs) || evidence.tickPairs.length !== 7) fail('seven per-tick raw pairs missing');
  for (let index=0; index<7; ++index) {
    const pair=evidence.tickPairs[index], frame=CPU_SEVEN_TICK_SAMPLES[index];
    if (!pair || pair.schema!==1 || pair.declaredBytes!==pairSize || pair.firstFailure!==0 || pair.equality!==1 ||
        pair.preFrame!==checkpointSourceFrame+index || pair.postFrame!==checkpointSourceFrame+index+1 ||
        !pair.rawFiveMatch || pair.baselinePad!==CPU_SEVEN_TICK_PAD44_CORPUS[index] ||
        pair.replayPad!==CPU_SEVEN_TICK_PAD44_CORPUS[index]) fail(`sample ${frame} raw pair mismatch`);
    if (index<6 && (pair.checkedDomains!==0 || pair.matchedDomains!==0)) fail(`sample ${frame} has premature endpoint domains`);
    if (index===6 && (pair.checkedDomains!==0x3fe || pair.matchedDomains!==0x3fe || pair.comparison.status!==0))
      fail('sample 306 endpoint typed domains missing');
  }
  const drawn=evidence.drawnPair;
  if (!drawn || drawn.schema!==1 || drawn.declaredBytes!==pairSize || drawn.firstFailure!==0 ||
      drawn.preFrame!==checkpointSourceFrame+7 || drawn.postFrame!==checkpointSourceFrame+7 ||
      drawn.commitResult!==0 || drawn.equality!==1 || drawn.checkedDomains!==0x3fe ||
      drawn.matchedDomains!==0x3fe || drawn.comparison.status!==0 || !drawn.rawFiveMatch ||
      drawn.baselinePad!==CPU_SEVEN_TICK_PAD44_CORPUS.at(-1) ||
      drawn.replayPad!==CPU_SEVEN_TICK_PAD44_CORPUS.at(-1)) fail('final drawn endpoint raw/typed evidence missing');
  if (!Array.isArray(evidence.diagnostics) || evidence.diagnostics.length!==16 ||
      evidence.diagnostics.some((row,index)=>!row||row.index!==index||row.bytes!==132||typeof row.base64!=='string'))
    fail('diagnostic boundary evidence missing');
  for (let index=8; index<15; ++index)
    if (evidence.diagnostics[index].base64!==evidence.diagnostics[7].base64)
      fail(`corrective sample ${300+index-8} changed draw diagnostics`);
  return true;
}

function inspectSourceObservation(module, bytes, mapping, stocks, slotCount) {
  return inspectInteractiveObservation(bytes, {classifyMotion: value =>
    module._melee_web_synctest_motion_boundary(value),
  isMarioKind: value => module._melee_web_synctest_is_mario_kind(value)===1,
  slotCount, expectedInputMapping:mapping, expectedStocks:stocks});
}

export async function runBrowserCpuSevenTick(predictedPage, authoritativePage, wire, checkpoint,
  expectedInputMapping, expectedStocks, previousFrame, beforeDiagnostics, helpers) {
  const mapping=validateSourceInputMapping(expectedInputMapping,beforeDiagnostics.slotCount);
  const localPort=mapping[0].rawController, remotePort=mapping[1].rawController;
  const checkpointPad=decode(checkpoint.pad);
  if (checkpointPad.length!==44 || !padSlot(checkpointPad,remotePort).equals(decode(CPU_SEVEN_TICK_INITIAL_REMOTE_PAD)))
    throw new Error('The agreed all-zero initial remote PAD differs from the same-prefix checkpoint');
  const schedule=createCpuSevenTickTimeline(localPort,remotePort,CPU_SEVEN_TICK_INITIAL_REMOTE_PAD);
  try {
    await predictedPage.evaluate(()=>{
      window.__slippiSynctest.cpuSevenTickPredictedProgress={status:'running',rows:[],simulatedThrough:299,
        route:'input-only-seven-tick-prediction'};
    });
    const predicted={rows:[]};
    for(let index=0;index<CPU_SEVEN_TICK_SAMPLES.length;index++){
      const tick=CPU_SEVEN_TICK_SAMPLES[index];
      const inputPlan=planCpuSevenTickForward(schedule,tick,CPU_SEVEN_TICK_PAD44_CORPUS[index]);
      const result=await predictedPage.evaluate(plan=>{
        const {module}=window.__slippiSynctest;
        const encode=bytes=>{let value='';for(let i=0;i<bytes.length;i+=0x6000)value+=String.fromCharCode(...bytes.subarray(i,i+0x6000));return btoa(value);};
        const read=(pointer,size,label)=>{
          if(!Number.isSafeInteger(pointer)||pointer<0||!Number.isSafeInteger(size)||size<=0||pointer+size>module.HEAPU8.length)
            throw new Error(`${label} byte extent`);
          return module.HEAPU8.slice(pointer,pointer+size);
        };
        const decodeDraw=new Function(`return (${plan.decodeDrawSource})`)();
        const validateMapping=new Function(`return (${plan.inputMappingSource})`)();
        const inspect=new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)(
          {interactive:0,entry:1,death:2,respawn:3,unknown:4},validateMapping);
        const inputMap=validateMapping(plan.mapping,decodeDraw(read(module._melee_web_synctest_draw_diagnostics(),132,'draw diagnostic')).slotCount);
        const diagnosticPointer=module._melee_web_synctest_draw_diagnostics();
        const priorDiag=read(diagnosticPointer,132,'draw diagnostic');
        const input=Uint8Array.from(atob(plan.frameBase64),char=>char.charCodeAt(0));
        if(input.length!==44)throw new Error(`Predicted frame ${plan.tick} is not 44 bytes`);
        const inputPointer=module._melee_web_snapshot_input();module.HEAPU8.set(input,inputPointer);
        if(encode(read(inputPointer,44,'source PAD'))!==plan.frameBase64)throw new Error('Predicted input injection drift');
        if(module._melee_web_synctest_draw_decision(1)!==0)throw new Error(`No-draw policy refused sample ${plan.tick}`);
        const step=module._melee_web_snapshot_step_raw();
        if(step!==1)throw new Error(`Predicted sample ${plan.tick} refused: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
        const observation=read(module._melee_web_snapshot_observation(),1280,'observation');
        const pcm=read(module._melee_web_snapshot_pcm(),4272,'PCM');
        const pad=read(inputPointer,44,'consumed PAD');
        const diagnostic=read(diagnosticPointer,132,'draw diagnostic');
        if(!diagnostic.every((value,index)=>value===priorDiag[index]))throw new Error(`Predicted sample ${plan.tick} drew`);
        if(encode(pad)!==plan.frameBase64)throw new Error(`Predicted sample ${plan.tick} did not consume the planned PAD`);
        const boundary=inspect(observation,{classifyMotion:value=>module._melee_web_synctest_motion_boundary(value),
          isMarioKind:value=>module._melee_web_synctest_is_mario_kind(value)===1,
          slotCount:decodeDraw(diagnostic).slotCount,expectedInputMapping:inputMap,expectedStocks:plan.stocks});
        if(!boundary.interactive||boundary.sourceFrame!==plan.checkpointSourceFrame+1)
          throw new Error(`Predicted interactive/source-frame boundary refused at ${plan.tick}: ${JSON.stringify(boundary.refusalReasons)}`);
        const row={sample:plan.tick,observationBase64:encode(observation),pcmBase64:encode(pcm),pad44Base64:encode(pad),
          boundary,drawDiagnosticsBase64:encode(diagnostic)};
        const progress=window.__slippiSynctest.cpuSevenTickPredictedProgress;
        if(!progress||progress.status!=='running'||progress.rows.length!==plan.index)
          throw new Error('Predicted seven-tick progress sequence is stale');
        progress.rows.push(row);
        return row;
      }, {index,tick:inputPlan.tick,frameBase64:encode(inputPlan.frame),
        checkpointSourceFrame:checkpoint.boundary.sourceFrame+index,mapping,stocks:expectedStocks,
        inputMappingSource:helpers.inputMappingSource,inspectSource:helpers.inspectSource,decodeDrawSource:helpers.decodeDrawSource});
      if(result.sample!==tick||result.pad44Base64!==encode(inputPlan.frame)||
         result.boundary?.sourceFrame!==checkpoint.boundary.sourceFrame+index+1||!result.boundary.interactive)
        throw new Error(`Seven-tick predicted source execution boundary refused at ${tick}`);
      // The native raw step, consumed PAD, no-draw diagnostics, and interactive
      // boundary have all passed before the input cursor advances.
      acknowledgeCpuSevenTickForward(schedule,inputPlan);
      result.timelineAcknowledged=true;
      await predictedPage.evaluate(({index,tick})=>{
        const progress=window.__slippiSynctest.cpuSevenTickPredictedProgress;
        const row=progress?.rows?.[index];
        if(!progress||progress.status!=='running'||row?.sample!==tick||progress.simulatedThrough!==undefined&&progress.simulatedThrough!==tick-1)
          throw new Error('Predicted seven-tick native boundary cannot acknowledge this input plan');
        row.timelineAcknowledged=true;progress.simulatedThrough=tick;
      },{index,tick});
      predicted.rows.push(result);
    }
    const guard=finishCpuSevenTickPrediction(schedule);

    const baseline=await authoritativePage.evaluate(async plan=>{
      const {module}=window.__slippiSynctest;
      const encode=bytes=>{let value='';for(let i=0;i<bytes.length;i+=0x6000)value+=String.fromCharCode(...bytes.subarray(i,i+0x6000));return btoa(value);};
      const read=(pointer,size,label)=>{
        if(!Number.isSafeInteger(pointer)||pointer<0||!Number.isSafeInteger(size)||size<=0||pointer+size>module.HEAPU8.length)
          throw new Error(`${label} byte extent`);
        return module.HEAPU8.slice(pointer,pointer+size);
      };
      const decodeDraw=new Function(`return (${plan.decodeDrawSource})`)();
      const validateMapping=new Function(`return (${plan.inputMappingSource})`)();
      const inspect=new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)(
        {interactive:0,entry:1,death:2,respawn:3,unknown:4},validateMapping);
      const validateFrame=new Function(`return (${plan.validateAuroraFrameSource})`)();
      const selectDraw=new Function('validateSourceInputMapping',`return (${plan.selectedDrawSource})`)(validateMapping);
      const map=validateMapping(plan.mapping,decodeDraw(read(module._melee_web_synctest_draw_diagnostics(),132,'draw diagnostic')).slotCount);
      const wait=new Function(`return (${plan.waitSource})`)();
      const progress={status:'running',route:'drawn-reference-no-draw-seven-tick-v1',commands:[],baseline:[],nativeEvidenceBase64:null,lastState:null};
      window.__slippiSynctest.cpuSevenTickProgress=progress;
      if(typeof SharedArrayBuffer!=='undefined'&&module.HEAPU8.buffer instanceof SharedArrayBuffer)
        throw new Error('Seven-tick route refuses shared Wasm memory');
      if(module._melee_web_snapshot_observation_size()!==1280||module._melee_web_snapshot_pcm_size()!==4272||
         module._melee_web_synctest_draw_diagnostics_size()!==132)throw new Error('Seven-tick source ABI drift');
      const checkpointObs=read(module._melee_web_snapshot_observation(),1280,'checkpoint observation');
      const checkpointPad=read(module._melee_web_snapshot_input(),44,'checkpoint PAD');
      if(encode(checkpointObs)!==plan.checkpointObservationBase64||encode(checkpointPad)!==plan.checkpointPadBase64||
         new DataView(checkpointObs.buffer).getUint32(0,true)!==plan.checkpointSourceFrame)
        throw new Error('Authoritative page checkpoint differs from its warmed prefix record');
      if(typeof module._melee_web_synctest_cpu_seven_tick_evidence!=='function'||
         typeof module._melee_web_synctest_cpu_seven_tick_evidence_bytes!=='function')
        throw new Error('Seven-tick evidence exports are unavailable');
      const write=(base64)=>{
        const bytes=Uint8Array.from(atob(base64),char=>char.charCodeAt(0));
        if(bytes.length!==44)throw new Error('Literal PAD corpus entry is not 44 bytes');
        const pointer=module._melee_web_snapshot_input();module.HEAPU8.set(bytes,pointer);
        if(encode(read(pointer,44,'injected PAD'))!==base64)throw new Error('Literal PAD injection drift');
      };
      const parseEvidence=raw=>{
        const view=new DataView(raw.buffer,raw.byteOffset,raw.byteLength),u32=offset=>view.getUint32(offset,true);
        const diagnosticMask=u32(56);
        if(u32(0)!==1||u32(4)!==plan.evidenceBytes||u32(8)!==7||(diagnosticMask&~0xffff)!==0)
          throw new Error('Seven-tick native evidence header drift');
        const diagnostics=Array.from({length:16},(_,index)=>{
          if(!(diagnosticMask&(1<<index)))return null;
          return {index,bytes:132,base64:encode(raw.slice(91400+index*132,91400+(index+1)*132))};
        });
        return {state:u32(12),firstFailure:u32(16),preFrame:u32(20),postFrame:u32(24),commitResult:u32(28),
          cpuEquality:u32(32),drawnEquality:u32(36),checkedCpu:u32(40),matchedCpu:u32(44),checkedDrawn:u32(48),
          matchedDrawn:u32(52),diagnosticMask,lastCommand:u32(60),baselineStepNoDrawMask:u32(64),
          replayStepNoDrawMask:u32(68),diagnostics};
      };
      const decodePair=(raw,index)=>{
        const offset=72+index*plan.pairBytes;
        const pair=raw.slice(offset,offset+plan.pairBytes),view=new DataView(pair.buffer,pair.byteOffset,pair.byteLength);
        if(view.getUint32(0,true)!==1||view.getUint32(4,true)!==plan.pairBytes)
          throw new Error(`Seven-tick pair ${index} schema drift`);
        return {preFrame:view.getUint32(16,true),postFrame:view.getUint32(20,true),
          baselineObservation:encode(pair.slice(32,1312)),baselinePcm:encode(pair.slice(2592,6864)),
          baselinePad:encode(pair.slice(11136,11180))};
      };
      const decodeEvidence=()=>{
        const size=module._melee_web_synctest_cpu_seven_tick_evidence_bytes();
        if(size!==plan.evidenceBytes)throw new Error('Seven-tick evidence ABI mismatch');
        const value=read(module._melee_web_synctest_cpu_seven_tick_evidence(),size,'seven-tick evidence');
        const summary=parseEvidence(value);
        progress.nativeEvidenceBase64=encode(value);progress.lastState=summary;
        return {raw:value,summary};
      };
      const command=async number=>{
        const record={command:number,outcome:'pending'};progress.commands.push(record);
        try{
          record.result=await module.ccall('melee_web_synctest_cpu_restore_command','number',['number'],[number],{async:true});
          record.outcome='returned';record.nativeSummary=decodeEvidence().summary;
          if(record.result!==0)throw new Error(`Seven-tick command ${number} refused ${record.result}: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
        }catch(error){
          if(record.outcome==='pending'){record.outcome='threw';record.error=String(error);}
          throw error;
        }
        return record.nativeSummary;
      };
      const configBytes=new Uint8Array(plan.wire.length*16),view=new DataView(configBytes.buffer);
      plan.wire.forEach((row,index)=>row.forEach((value,field)=>view.setUint32(index*16+field*4,value,true)));
      const configPointer=module._malloc(configBytes.length);if(!configPointer)throw new Error('Seven-tick wire allocation failed');
      try{module.HEAPU8.set(configBytes,configPointer);
        const configured=module._melee_web_synctest_cpu_restore_configure(configPointer,plan.wire.length);
        progress.configurationResult=configured;if(configured!==0)throw new Error(`CPU configuration refused ${configured}`);
      }finally{module._free(configPointer);}
      let evidence=await command(14);
      if(evidence.state!==14||evidence.preFrame!==plan.checkpointSourceFrame||evidence.postFrame!==plan.checkpointSourceFrame+7||
         evidence.diagnosticMask!==1||evidence.diagnostics[0]?.base64!==plan.checkpointDrawDiagnosticsBase64)
        throw new Error('Seven-tick CAPTURE state/frame/draw-boundary contract missing');
      let previousDiagnostic=read(module._melee_web_synctest_draw_diagnostics(),132,'initial draw diagnostic');
      const baselineRows=[];
      let previousGpu={beginFrameId:plan.previousFrame.beginFrameId,endFrameId:plan.previousFrame.endFrameId};
      for(let index=0;index<7;++index){
        const sample=300+index,expected=plan.padCorpus[index];
        const drawBefore=read(module._melee_web_synctest_draw_diagnostics(),132,'pre-step draw diagnostics');
        if(!drawBefore.every((value,offset)=>value===previousDiagnostic[offset]))
          throw new Error(`Prior draw diagnostics drifted before authoritative sample ${sample}`);
        write(expected);
        evidence=await command(15+index);
        const pair=decodePair(Uint8Array.from(atob(progress.nativeEvidenceBase64),char=>char.charCodeAt(0)),index);
        if(!pair||pair.preFrame!==plan.checkpointSourceFrame+index||pair.postFrame!==plan.checkpointSourceFrame+index+1||
           pair.baselinePad!==expected)throw new Error(`Seven-tick authoritative pair ${sample} drift`);
        if(evidence.lastCommand!==15+index||!(evidence.baselineStepNoDrawMask&(1<<index))||
           !evidence.diagnostics[index]||evidence.diagnostics[index].base64!==encode(drawBefore))
          throw new Error(`Authoritative source sample ${sample} changed draw diagnostics before its draw`);
        const obs=Uint8Array.from(atob(pair.baselineObservation),char=>char.charCodeAt(0));
        const boundary=inspect(obs,{classifyMotion:value=>module._melee_web_synctest_motion_boundary(value),
          isMarioKind:value=>module._melee_web_synctest_is_mario_kind(value)===1,
          slotCount:decodeDraw(drawBefore).slotCount,expectedInputMapping:map,expectedStocks:plan.stocks});
        if(!boundary.interactive||boundary.sourceFrame!==plan.checkpointSourceFrame+index+1)
          throw new Error(`Authoritative interactive boundary refused at ${sample}: ${JSON.stringify(boundary.refusalReasons)}`);
        const gpu=await wait(module,sample);
        const afterDraw=read(module._melee_web_synctest_draw_diagnostics(),132,'post-draw diagnostics');
        const beforeDecoded=decodeDraw(drawBefore),afterDecoded=decodeDraw(afterDraw);
        const selected=selectDraw(beforeDecoded,afterDecoded,boundary.players,map);
        const next=validateFrame(gpu,previousGpu.beginFrameId,previousGpu.endFrameId);
        previousGpu=next;previousDiagnostic=afterDraw;
        baselineRows.push({sample,observationBase64:pair.baselineObservation,pcmBase64:pair.baselinePcm,
          pad44Base64:pair.baselinePad,boundary,drawBeforeBase64:encode(drawBefore),
          drawAfterBase64:encode(afterDraw),selectedDraw:selected,gpuStatus:gpu});
        progress.baseline=baselineRows;
      }
      await wait(module,'sample306-baseline-final-idle');
      return {baselineRows,nativeEvidenceBase64:progress.nativeEvidenceBase64,lastGpuStatus:previousGpu,
        finalBaselineDiagnosticBase64:encode(previousDiagnostic)};
    }, {wire, evidenceBytes:CPU_SEVEN_TICK_EVIDENCE_BYTES,pairBytes:CPU_SEVEN_TICK_PAIR_BYTES,
      checkpointSourceFrame:checkpoint.boundary.sourceFrame,
      checkpointObservationBase64:checkpoint.observation,checkpointPadBase64:checkpoint.pad,
      checkpointDrawDiagnosticsBase64:checkpoint.drawDiagnostics,previousFrame,
      padCorpus:CPU_SEVEN_TICK_PAD44_CORPUS,mapping,stocks:expectedStocks,
      inputMappingSource:helpers.inputMappingSource,
      inspectSource:helpers.inspectSource,selectedDrawSource:helpers.selectedDrawSource,
      decodeDrawSource:helpers.decodeDrawSource,
      validateAuroraFrameSource:validateAuroraFrame.toString(),
      waitSource:waitForAuroraSubmission.toString()});

    const correction=planCpuSevenTickCorrection(schedule.timeline,baseline.baselineRows,localPort,remotePort);
    const replayed=await authoritativePage.evaluate(async plan=>{
      const {module}=window.__slippiSynctest;
      const encode=bytes=>{let value='';for(let i=0;i<bytes.length;i+=0x6000)value+=String.fromCharCode(...bytes.subarray(i,i+0x6000));return btoa(value);};
      const read=(pointer,size,label)=>{
        if(!Number.isSafeInteger(pointer)||pointer<0||!Number.isSafeInteger(size)||size<=0||pointer+size>module.HEAPU8.length)
          throw new Error(`${label} byte extent`);
        return module.HEAPU8.slice(pointer,pointer+size);
      };
      const decodeDraw=new Function(`return (${plan.decodeDrawSource})`)();
      const validateMapping=new Function(`return (${plan.inputMappingSource})`)();
      const inspect=new Function('DRAW_MOTION_BOUNDARY','validateSourceInputMapping',`return (${plan.inspectSource})`)(
        {interactive:0,entry:1,death:2,respawn:3,unknown:4},validateMapping);
      const map=validateMapping(plan.mapping,decodeDraw(read(module._melee_web_synctest_draw_diagnostics(),132,'draw diagnostic')).slotCount);
      const wait=new Function(`return (${plan.waitSource})`)();
      const progress=window.__slippiSynctest.cpuSevenTickProgress;
      if(!progress||progress.lastState?.state!==15||progress.commands.length!==8)
        throw new Error('Seven-tick authoritative baseline did not complete before correction');
      const parseEvidence=raw=>{
        const view=new DataView(raw.buffer,raw.byteOffset,raw.byteLength),u32=offset=>view.getUint32(offset,true);
        const diagnosticMask=u32(56);
        if(u32(0)!==1||u32(4)!==plan.evidenceBytes||u32(8)!==7||(diagnosticMask&~0xffff)!==0)
          throw new Error('Seven-tick native evidence header drift');
        const diagnostics=Array.from({length:16},(_,index)=>{
          if(!(diagnosticMask&(1<<index)))return null;
          return {index,bytes:132,base64:encode(raw.slice(91400+index*132,91400+(index+1)*132))};
        });
        return {state:u32(12),firstFailure:u32(16),preFrame:u32(20),postFrame:u32(24),commitResult:u32(28),
          cpuEquality:u32(32),drawnEquality:u32(36),checkedCpu:u32(40),matchedCpu:u32(44),checkedDrawn:u32(48),
          matchedDrawn:u32(52),diagnosticMask,lastCommand:u32(60),baselineStepNoDrawMask:u32(64),
          replayStepNoDrawMask:u32(68),diagnostics};
      };
      const decodePair=(raw,offset)=>{
        const pair=raw.slice(offset,offset+plan.pairBytes),view=new DataView(pair.buffer,pair.byteOffset,pair.byteLength);
        if(view.getUint32(0,true)!==1||view.getUint32(4,true)!==plan.pairBytes)
          throw new Error('Seven-tick raw pair schema drift');
        return {schema:view.getUint32(0,true),declaredBytes:view.getUint32(4,true),state:view.getUint32(8,true),
          firstFailure:view.getUint32(12,true),preFrame:view.getUint32(16,true),postFrame:view.getUint32(20,true),
          commitResult:view.getUint32(24,true),equality:view.getUint32(28,true),
          baselineObservation:encode(pair.slice(32,1312)),replayObservation:encode(pair.slice(1312,2592)),
          baselinePcm:encode(pair.slice(2592,6864)),replayPcm:encode(pair.slice(6864,11136)),
          baselinePad:encode(pair.slice(11136,11180)),replayPad:encode(pair.slice(11180,11224))};
      };
      const command=async number=>{
        const record={command:number,outcome:'pending'};progress.commands.push(record);
        try{
          record.result=await module.ccall('melee_web_synctest_cpu_restore_command','number',['number'],[number],{async:true});
          record.outcome='returned';
          const raw=read(module._melee_web_synctest_cpu_seven_tick_evidence(),plan.evidenceBytes,'seven-tick evidence');
          record.nativeSummary=parseEvidence(raw);
          progress.nativeEvidenceBase64=encode(raw);progress.lastState=record.nativeSummary;
          if(record.result!==0)throw new Error(`Seven-tick command ${number} refused ${record.result}: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
        }catch(error){if(record.outcome==='pending'){record.outcome='threw';record.error=String(error);}throw error;}
        return record.nativeSummary;
      };
      const write=base64=>{
        const bytes=Uint8Array.from(atob(base64),char=>char.charCodeAt(0));
        if(bytes.length!==44)throw new Error('Correction frame is not 44 bytes');
        const pointer=module._melee_web_snapshot_input();module.HEAPU8.set(bytes,pointer);
        if(encode(read(pointer,44,'corrected PAD'))!==base64)throw new Error('Correction input injection drift');
      };
      let evidence=await command(22);
      if(evidence.state!==16||evidence.commitResult!==0||evidence.diagnosticMask!==255)
        throw new Error('Seven-tick typed restore did not arm exact seven-frame replay');
      const replayRows=[];
      for(let index=0;index<7;++index){
        const frame=plan.replayFrames[index],sample=300+index;write(frame.frameBase64);
        evidence=await command(23+index);
        const raw=Uint8Array.from(atob(progress.nativeEvidenceBase64),char=>char.charCodeAt(0));
        const pair=decodePair(raw,72+index*plan.pairBytes);
        if(pair.replayPad!==frame.frameBase64||pair.baselinePad!==frame.frameBase64||pair.equality!==1||
           pair.firstFailure!==0||!(evidence.replayStepNoDrawMask&(1<<index))||
           !evidence.diagnostics[8+index]||evidence.diagnostics[8+index].base64!==evidence.diagnostics[7].base64)
          throw new Error(`Corrected source pair differs at sample ${sample}`);
        const obs=Uint8Array.from(atob(pair.replayObservation),char=>char.charCodeAt(0));
        const boundary=inspect(obs,{classifyMotion:value=>module._melee_web_synctest_motion_boundary(value),
          isMarioKind:value=>module._melee_web_synctest_is_mario_kind(value)===1,
          slotCount:decodeDraw(read(module._melee_web_synctest_draw_diagnostics(),132,'draw diagnostic')).slotCount,
          expectedInputMapping:map,expectedStocks:plan.stocks});
        if(!boundary.interactive||boundary.sourceFrame!==plan.checkpointSourceFrame+index+1)
          throw new Error(`Corrected interactive boundary refused at ${sample}: ${JSON.stringify(boundary.refusalReasons)}`);
        replayRows.push({sample,observationBase64:pair.replayObservation,pcmBase64:pair.replayPcm,
          pad44Base64:pair.replayPad,boundary,drawDiagnosticBase64:evidence.diagnostics[8+index]?.base64});
      }
      if(evidence.state!==17||evidence.cpuEquality!==1||evidence.checkedCpu!==0x3fe||evidence.matchedCpu!==0x3fe)
        throw new Error('Seven-tick typed CPU endpoint equality missing after replay');
      evidence=await command(30);
      const finalGpu=await wait(module,306);
      evidence=await command(31);
      const finalEvidenceBytes=Uint8Array.from(atob(progress.nativeEvidenceBase64),char=>char.charCodeAt(0));
      if(evidence.state!==5||evidence.lastCommand!==31||evidence.diagnosticMask!==0xffff||evidence.firstFailure!==0||
         evidence.cpuEquality!==1||evidence.drawnEquality!==1||evidence.checkedCpu!==0x3fe||
         evidence.matchedCpu!==0x3fe||evidence.checkedDrawn!==0x3fe||evidence.matchedDrawn!==0x3fe)
        throw new Error('Seven-tick final native typed endpoint contract missing');
      const drawn=decodePair(finalEvidenceBytes,79984),finalDraw=evidence.diagnostics[15].base64;
      const finalObs=read(module._melee_web_snapshot_observation(),1280,'final observation');
      const finalPcm=read(module._melee_web_snapshot_pcm(),4272,'final PCM');
      const finalPad=read(module._melee_web_snapshot_input(),44,'final PAD');
      if(encode(finalObs)!==drawn.replayObservation||encode(finalPcm)!==drawn.replayPcm||encode(finalPad)!==drawn.replayPad||
         encode(read(module._melee_web_synctest_draw_diagnostics(),132,'final draw diagnostics'))!==finalDraw)
        throw new Error('Live final-present bytes differ from the strict drawn endpoint');
      const finalBoundary=inspect(finalObs,{classifyMotion:value=>module._melee_web_synctest_motion_boundary(value),
        isMarioKind:value=>module._melee_web_synctest_is_mario_kind(value)===1,
        slotCount:decodeDraw(read(module._melee_web_synctest_draw_diagnostics(),132,'final draw diagnostic')).slotCount,
        expectedInputMapping:map,expectedStocks:plan.stocks});
      if(!finalBoundary.interactive||finalBoundary.sourceFrame!==plan.checkpointSourceFrame+7)
        throw new Error('Final present interactive source-frame boundary refused');
      progress.status='drawn-reference-no-draw-seven-tick-component-complete';
      progress.replay=replayRows;progress.finalGpuStatus=finalGpu;
      return {observation:encode(finalObs),pcm:encode(finalPcm),pad:encode(finalPad),drawDiagnostics:finalDraw,
        boundary:finalBoundary,status:finalGpu,cpuSevenTick:{...progress,nativeEvidenceBase64:progress.nativeEvidenceBase64}};
    }, {evidenceBytes:CPU_SEVEN_TICK_EVIDENCE_BYTES,pairBytes:CPU_SEVEN_TICK_PAIR_BYTES,
      checkpointSourceFrame:checkpoint.boundary.sourceFrame,
      mapping,stocks:expectedStocks,replayFrames:correction.frames.map(row=>({tick:row.tick,frameBase64:encode(row.frame)})),
      inputMappingSource:helpers.inputMappingSource,
      inspectSource:helpers.inspectSource,decodeDrawSource:helpers.decodeDrawSource,
      waitSource:waitForAuroraSubmission.toString()});

    const native=decodeCpuSevenTickEvidence(decode(replayed.cpuSevenTick.nativeEvidenceBase64));
    validateCpuSevenTickFinalEvidence(native,checkpoint.boundary.sourceFrame);
    const baselineRows=baseline.baselineRows;
    const predictedMotion=schedule.frames.map((_,index)=>predicted.rows[index].boundary.players.find(row=>row.rawController===remotePort)?.motion);
    const authoritativeMotion=baselineRows.map(row=>row.boundary.players.find(player=>player.rawController===remotePort)?.motion);
    if(predictedMotion.some(value=>!Number.isInteger(value))||authoritativeMotion.some(value=>!Number.isInteger(value))||
       !predictedMotion.some((value,index)=>value!==authoritativeMotion[index]))
      throw new Error('Withheld remote prediction did not produce a remote fighter motion divergence');
    schedule.timeline.commitReplay(correction.replay);
    const timelineStatus=schedule.timeline.status();
    if(timelineStatus.dirty_from!==null||timelineStatus.simulated_through!==306)
      throw new Error(`Seven-tick timeline did not acknowledge replay: ${JSON.stringify(timelineStatus)}`);
    const browserOutput={...replayed,
      cpuSevenTick:{...replayed.cpuSevenTick,nativeEvidence:native,
        route:'drawn-reference-no-draw-seven-tick-v1',checkpointSourceFrame:checkpoint.boundary.sourceFrame,
        baselineRows,predictedRows:predicted.rows,correctedRows:replayed.cpuSevenTick.replay,
        timeline:{initialRemotePad:CPU_SEVEN_TICK_INITIAL_REMOTE_PAD,localPort,remotePort,
          maxPredictionTicks:ROLLBACK_MAX_PREDICTION_TICKS,predictionLimit:{tick:307,status:guard.status,
            reason:guard.reason,localPadBase64:guard.localPadBase64,nativeStepExecuted:guard.nativeStepExecuted},earliestChangedTick:correction.earliestChanged,
          remoteContiguousThrough:correction.remoteContiguousThrough,dirtyFromAfterCommit:timelineStatus.dirty_from,
          speculativeTicksAfterReplay:correction.replay.speculative_ticks,
          replayFrames:correction.frames.map(row=>({tick:row.tick,frameBase64:encode(row.frame)})),
          originalLocalPadsPreserved:true,
          remoteMotionByTick:schedule.frames.map((row,index)=>({tick:row.tick,
            predicted:predictedMotion[index],authoritative:authoritativeMotion[index],
            differs:predictedMotion[index]!==authoritativeMotion[index]}))}}};
    delete browserOutput.cpuSevenTickProgress;
    return browserOutput;
  } catch (error) {
    try { error.cpuSevenTickPredictedPartial=await predictedPage.evaluate(() => window.__slippiSynctest?.cpuSevenTickPredictedProgress||null); }
    catch (readError) { error.cpuSevenTickPredictedPartialReadError=String(readError); }
    try { error.cpuSevenTickPartial=await authoritativePage.evaluate(() => window.__slippiSynctest?.cpuSevenTickProgress||null); }
    catch (readError) { error.cpuSevenTickPartialReadError=String(readError); }
    throw error;
  } finally {
    schedule.timeline.dispose();
  }
}
