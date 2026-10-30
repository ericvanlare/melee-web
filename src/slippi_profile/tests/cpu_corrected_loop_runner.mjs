// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded same-match input stream: one capsule at a time, strict local
// reference/correction comparisons, and streamed evidence before retirement.
import {MatchInputTimeline,ROLLBACK_MAX_PREDICTION_TICKS} from '../../../scripts/net_rollback_input_timeline.mjs';
import {
  CPU_SEVEN_TICK_EVIDENCE_BYTES,
  decodeCpuCorrectedLoopEvidence,
} from './cpu_seven_tick_runner.mjs';
import {
  CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,buildCpuCorrectedTwoWindowFirstCorpus,
  buildCpuCorrectedTwoWindowSecondCorpus,
} from './cpu_corrected_two_window_runner.mjs';
import {correctedPageDefaults,operateCorrectedInPage,recordPredictedActionDivergence} from './cpu_corrected_runner.mjs';
import {
  CPU_ENDPOINT_PROVENANCE,CPU_ENDPOINT_VIEW,collectCpuEndpointInPage,
  compareCpuParticleEndpoints,compareCpuReferenceParticleEndpoints,
  decodeCpuParticleEndpointResult,
  validateCpuParticleEndpointAgainstPre,
} from './cpu_endpoint.mjs';
import {
  decodeDrawDiagnostics,validateAuroraFrame,validateSelectedDrawPath,
  validateSourceInputMapping,
} from './runner_contract.mjs';

export const CPU_CORRECTED_LOOP_SCHEMA=3;
export const CPU_CORRECTED_LOOP_METADATA_BYTES=40;
export const CPU_CORRECTED_LOOP_SPANS=Object.freeze([1,6,7,7,7,7,7,2]);
export const CPU_CORRECTED_LOOP_FIRST_SAMPLE=300;
export const CPU_CORRECTED_LOOP_LAST_SAMPLE=343;
export const CPU_CORRECTED_LOOP_NEUTRAL_PAD44=CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44;
export const CPU_CORRECTED_LOOP_REMOTE_B_PAD44=
  'AAAAAAAAAAAAAAACAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=';
const PAD_BYTES=11,FRAME_BYTES=44,INITIAL_REMOTE_PAD='AAAAAAAAAAAAAAA=';
const rawLanes=Object.freeze(['Observation','Pcm','Pad','RngProfile','Transfer']);
const decode=value=>Buffer.from(value,'base64');
const encode=value=>Buffer.from(value).toString('base64');
const equal=(a,b)=>Buffer.from(a).equals(Buffer.from(b));
const padSlot=(pad,port)=>pad.subarray(port*PAD_BYTES,(port+1)*PAD_BYTES);

function validateLiteralPad(value,label){
  const pad=decode(value);
  if(pad.length!==FRAME_BYTES||pad[32]!==0xff||pad[43]!==0xff)
    throw new Error(`${label} must be an exact 44-byte PAD with ports 2/3 disconnected`);
  return pad;
}

export function buildCpuCorrectedLoopCapsules(localPort=0,remotePort=1){
  if(localPort!==0||remotePort!==1)
    throw new Error('Corrected loop witness is pinned to source ports 0/1');
  const first=buildCpuCorrectedTwoWindowFirstCorpus(localPort,remotePort);
  const branchB=buildCpuCorrectedTwoWindowSecondCorpus();
  const neutral=CPU_CORRECTED_LOOP_NEUTRAL_PAD44;
  const specs=[
    {startSample:300,span:1,kind:'speculative-s4',rows:[first[0]]},
    {startSample:301,span:6,kind:'confirmed-s4-tail',rows:first.slice(1)},
    ...Array.from({length:5},(_,index)=>({startSample:307+index*7,span:7,
      kind:'confirmed-neutral',rows:Array(7).fill(neutral)})),
    {startSample:342,span:2,kind:'speculative-b-neutral',rows:branchB.slice(0,2)},
  ];
  if(specs.length!==8||specs.reduce((sum,row)=>sum+row.span,0)!==44)
    throw new Error('Corrected loop capsule recipe extent drift');
  let next=CPU_CORRECTED_LOOP_FIRST_SAMPLE;
  for(const [index,capsule] of specs.entries()){
    if(capsule.startSample!==next||capsule.rows.length!==capsule.span||
       capsule.span!==CPU_CORRECTED_LOOP_SPANS[index])
      throw new Error(`Corrected loop capsule ${index} is not a contiguous supported span`);
    capsule.rows.forEach((row,offset)=>validateLiteralPad(row,`sample ${next+offset}`));
    next+=capsule.span;
  }
  if(next!==CPU_CORRECTED_LOOP_LAST_SAMPLE+1||
     specs[0].rows[0]!==first[0]||specs[1].rows.some((row,index)=>row!==first[index+1])||
     specs.slice(2,7).some(capsule=>capsule.rows.some(row=>row!==neutral))||
     specs[7].rows[0]!==CPU_CORRECTED_LOOP_REMOTE_B_PAD44||specs[7].rows[1]!==neutral)
    throw new Error('Corrected loop literal input history changed');
  return Object.freeze(specs.map(capsule=>Object.freeze({...capsule,rows:Object.freeze([...capsule.rows])})));
}

export function createCpuCorrectedLoopSchedule(localPort=0,remotePort=1){
  if(!Number.isInteger(localPort)||!Number.isInteger(remotePort)||localPort===remotePort||
     localPort<0||remotePort<0||localPort>3||remotePort>3)
    throw new Error('Corrected loop requires two distinct source ports');
  const seed=decode(INITIAL_REMOTE_PAD);
  const timeline=new MatchInputTimeline({localPort,remotePort,startTick:300,initialRemotePad:seed,
    maxHistoryTicks:128,maxLookaheadTicks:8});
  return {timeline,localPort,remotePort,initialRemotePad:seed,lastRemotePad:Buffer.from(seed),pendingPlan:null,
    lastAcknowledged:299};
}

export function planCpuCorrectedLoopForward(schedule,tick,pad44,{speculative=false,remotePad=null}={}){
  if(!schedule||schedule.pendingPlan||!Number.isSafeInteger(tick))
    throw new Error('Corrected loop forward plan is stale or malformed');
  const actual=validateLiteralPad(pad44,`sample ${tick}`),before=schedule.timeline.status();
  if(tick!==before.simulated_through+1||tick!==schedule.lastAcknowledged+1)
    throw new Error(`Corrected loop sample ${tick} is not contiguous`);
  const authoritativeRemote=remotePad===null?padSlot(actual,schedule.remotePort):Buffer.from(remotePad);
  if(authoritativeRemote.length!==PAD_BYTES)throw new Error(`Corrected loop remote PAD ${tick} must contain 11 bytes`);
  if(speculative&&remotePad!==null)throw new Error(`Speculative corrected loop sample ${tick} cannot accept PAD before its final draw`);
  schedule.timeline.recordLocal(tick,padSlot(actual,schedule.localPort));
  if(!speculative){
    const accepted=schedule.timeline.acceptRemote(tick,authoritativeRemote);
    if(accepted.status!=='accepted'||accepted.earliest_changed_tick!==null)
      throw new Error(`Confirmed remote PAD ${tick} refused or dirtied history`);
  }
  const plan=schedule.timeline.planForward();
  const expected=Buffer.from(actual);
  expected.set(speculative?schedule.lastRemotePad:authoritativeRemote,
    schedule.remotePort*PAD_BYTES);
  const expectedSpeculative=speculative?tick-before.remote_contiguous_through:0;
  if(plan.kind!=='forward'||plan.tick!==tick||plan.frame.length!==FRAME_BYTES||
     plan.speculative_ticks!==expectedSpeculative||
     plan.remote_source!==(speculative?'last_known_prediction':'actual')||
     !equal(plan.frame,expected))
    throw new Error(`Corrected loop timeline prediction/confirmation drift at ${tick}`);
  schedule.pendingPlan=plan;
  return plan;
}

export function acknowledgeCpuCorrectedLoopForward(schedule,plan){
  if(!schedule||!plan||schedule.pendingPlan!==plan||plan.tick!==schedule.lastAcknowledged+1)
    throw new Error('Corrected loop forward acknowledgement is stale');
  schedule.timeline.commitForward(plan);
  schedule.pendingPlan=null;schedule.lastAcknowledged=plan.tick;
  if(plan.remote_source==='actual')schedule.lastRemotePad=Buffer.from(padSlot(Buffer.from(plan.frame),schedule.remotePort));
  return schedule.timeline.status();
}

export function planCpuCorrectedLoopReplay(schedule,baselineRows,actualRows,startSample){
  if(!schedule||schedule.pendingPlan||!Array.isArray(baselineRows)||!Array.isArray(actualRows)||
     baselineRows.length<1||baselineRows.length>ROLLBACK_MAX_PREDICTION_TICKS||
     actualRows.length!==baselineRows.length||!Number.isSafeInteger(startSample)||
     schedule.lastAcknowledged!==startSample+baselineRows.length-1)
    throw new Error('Corrected loop replay requires the complete executed capsule');
  let earliestChanged=null;
  for(let index=0;index<actualRows.length;index++){
    const tick=startSample+index,row=actualRows[index],pad=validateLiteralPad(row?.pad44Base64,`actual sample ${tick}`);
    if(row.sample!==tick)throw new Error(`Corrected loop authority sample drift at ${tick}`);
    const accepted=schedule.timeline.acceptRemote(tick,padSlot(pad,schedule.remotePort));
    if(accepted.status!=='accepted')throw new Error(`Corrected loop remote PAD ${tick} refused`);
    if(earliestChanged===null&&accepted.earliest_changed_tick!==null)
      earliestChanged=accepted.earliest_changed_tick;
  }
  const status=schedule.timeline.status(),end=startSample+actualRows.length-1;
  if(earliestChanged===null||status.dirty_from!==earliestChanged||
     status.remote_contiguous_through!==end)
    throw new Error('Corrected loop did not produce a bounded dirty suffix');
  const replay=schedule.timeline.planReplay();
  if(replay.kind!=='replay'||replay.from!==earliestChanged||replay.through!==end||
     replay.frames.length!==end-earliestChanged+1||replay.speculative_ticks!==0)
    throw new Error('Corrected loop dirty-suffix plan extent/confirmation drift');
  const dirtyOffset=earliestChanged-startSample;
  const frames=[];
  for(let index=0;index<dirtyOffset;index++){
    const baseline=baselineRows[index],actual=validateLiteralPad(actualRows[index].pad44Base64,
      `unchanged prefix sample ${startSample+index}`);
    if(baseline.tick!==startSample+index||!equal(decode(baseline.predictedPad44Base64),actual))
      throw new Error(`Corrected loop unchanged prefix is not byte-identical at ${startSample+index}`);
    frames.push({tick:baseline.tick,frame:actual,unchangedPrefix:true});
  }
  for(const [index,row] of replay.frames.entries()){
    const fullIndex=dirtyOffset+index,tick=startSample+fullIndex;
    const actual=validateLiteralPad(actualRows[fullIndex].pad44Base64,`replay sample ${tick}`);
    if(row.tick!==tick||!equal(row.local_pad,padSlot(actual,schedule.localPort))||
       !equal(row.remote_pad,padSlot(actual,schedule.remotePort))||!equal(row.frame,actual))
      throw new Error(`Corrected loop replay changed local/confirmed PAD at ${tick}`);
    frames.push({tick,frame:Buffer.from(row.frame),unchangedPrefix:false});
  }
  if(frames.length!==actualRows.length)throw new Error('Corrected loop must execute the whole capsule');
  return {earliestChanged,dirtyFrom:status.dirty_from,through:end,replay,frames};
}

export function decodeCpuCorrectedLoopMetadata(input){
  const bytes=Buffer.from(input);
  if(bytes.length!==CPU_CORRECTED_LOOP_METADATA_BYTES)throw new Error('Corrected loop metadata extent');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const value={schema:view.getUint32(0,true),bytes:view.getUint32(4,true),
    windowsCaptured:view.getUint32(8,true),windowsRetired:view.getUint32(12,true),
    activeCapsules:view.getUint32(16,true),maxActiveCapsules:view.getUint32(20,true),
    windowFirstSample:view.getUint32(24,true),windowPreFrame:view.getUint32(28,true),
    windowPostFrame:view.getUint32(32,true),windowSpan:view.getUint32(36,true)};
  if(value.schema!==CPU_CORRECTED_LOOP_SCHEMA||value.bytes!==CPU_CORRECTED_LOOP_METADATA_BYTES||
     value.reserved!==undefined||value.activeCapsules>1||value.maxActiveCapsules>1||
     value.windowsRetired>value.windowsCaptured)
    throw new Error('Corrected loop metadata schema/counters');
  return value;
}

export function requireCpuCorrectedLoopMetadata(value,expected,label){
  for(const [key,want] of Object.entries(expected))if(value?.[key]!==want)
    throw new Error(`${label} metadata ${key}=${value?.[key]}, expected ${want}`);
  return value;
}

function expectedLoopPhase(command,span){
  if(command===32)return 1;
  if(command>=33&&command<=32+span)return command===32+span?3:2;
  if(command===40)return 4;
  if(command===41)return 5;
  if(command===42)return 6;
  if(command>=43&&command<=42+span)return command===42+span?7:6;
  if(command===50)return 8;
  if(command===51)return 9;
  return 0;
}

export function validateCpuCorrectedLoopCommandEvidence(evidence,command,{preFrame,span}={}){
  if(!Number.isInteger(span)||span<1||span>7||!Number.isSafeInteger(preFrame)||preFrame<0||preFrame>0xffffffff-span||
     !evidence||evidence.schema!==3||evidence.bytes!==CPU_SEVEN_TICK_EVIDENCE_BYTES||
     evidence.mode!==span||evidence.lastCommand!==command||!expectedLoopPhase(command,span)||
     evidence.state!==expectedLoopPhase(command,span)||
     evidence.firstFailure!==0||evidence.preFrame!==preFrame||evidence.postFrame!==preFrame+span||
     evidence.cpuEquality!==0||evidence.drawnEquality!==0||evidence.checkedCpu!==0||
     evidence.matchedCpu!==0||evidence.checkedDrawn!==0||evidence.matchedDrawn!==0)
    throw new Error(`Corrected loop command ${command} schema/span/phase/source evidence drift`);
  const baselineSteps=command>=33&&command<=32+span?command-32:
    command>=40?span:0;
  const replaySteps=command>=43&&command<=42+span?command-42:
    command>=50?span:0;
  const mask=count=>(1<<count)-1;
  if(evidence.baselineStepNoDrawMask!==mask(baselineSteps)||
     evidence.replayStepNoDrawMask!==mask(replaySteps))
    throw new Error(`Corrected loop command ${command} source-step masks differ`);
  const baselineDiagnostics=mask(Math.max(1,baselineSteps));
  const diagnosticMask=baselineDiagnostics|(command>=41?1<<7:0)|
    (mask(replaySteps)<<8)|(command>=51?1<<15:0);
  if(evidence.diagnosticMask!==diagnosticMask)
    throw new Error(`Corrected loop command ${command} diagnostic slots differ`);
  if(command===42||command>=43)
    if(evidence.commitResult!==0)throw new Error(`Corrected loop command ${command} lacks a successful restore`);
  return evidence;
}

export function compareCpuCorrectedLoopRawFive(reference,corrected,label){
  for(const lane of rawLanes){
    const left=decode(reference?.[lane]??''),right=decode(corrected?.[lane]??'');
    const sizes={Observation:1280,Pcm:4272,Pad:44,RngProfile:32,Transfer:40};
    if(left.length!==sizes[lane]||right.length!==sizes[lane])throw new Error(`${label} ${lane} raw extent`);
    if(!left.equals(right)){
      let offset=0;while(offset<left.length&&left[offset]===right[offset])++offset;
      throw new Error(`${label} raw ${lane} differs at byte ${offset}`);
    }
  }
  return {equal:true,lanes:[...rawLanes]};
}

function rawFive(pair,lane){return Object.fromEntries(rawLanes.map(name=>[name,pair[`${lane}${name}`]]));}

function validateSinkReceipt(receipt){
  if(!receipt||typeof receipt.path!=='string'||!receipt.path||
     typeof receipt.sha256!=='string'||!/^[0-9a-f]{64}$/.test(receipt.sha256)||
     !Number.isSafeInteger(receipt.bytes)||receipt.bytes<0)
    throw new Error('Corrected loop evidence sink returned an invalid receipt');
  return receipt;
}

export const CPU_LOOP_MOTOR_EVIDENCE_BYTES=1016;
export const CPU_LOOP_MOTOR_RECEIPT_BYTES=64;
export const CPU_LOOP_MOTOR_LANE=Object.freeze({none:0,baseline:1,replay:2,corrected:3});
const MOTOR_SEQUENCE_BYTES=492,MOTOR_EVENT_CAPACITY=28;
const motorIdentityKeys=['token','world','contextEpoch','firstFrame','span'];

/** Native integers remain exact even above JavaScript's safe integer range. */
export function decodeCpuCorrectedLoopMotorEvidence(input){
  const bytes=Buffer.from(input);
  if(bytes.length!==CPU_LOOP_MOTOR_EVIDENCE_BYTES)throw new Error('Loop motor evidence extent');
  const u32=offset=>bytes.readUInt32LE(offset);
  const u64=offset=>(BigInt(u32(offset))|(BigInt(u32(offset+4))<<32n)).toString();
  const sequence=offset=>{
    const state=u32(offset),span=u32(offset+4),firstFrame=u32(offset+8),framesSeen=u32(offset+12),
      eventCount=u32(offset+16);
    if(![0,1,2].includes(state)||eventCount>MOTOR_EVENT_CAPACITY)
      throw new Error('Loop motor sequence state/count');
    if(state===0){
      if(!bytes.subarray(offset,offset+MOTOR_SEQUENCE_BYTES).every(value=>value===0))
        throw new Error('Loop motor absent sequence is not zero');
    }else if(span<1||span>7||firstFrame>0xffffffff-span||framesSeen>span||
             state===1&&framesSeen!==span)
      throw new Error('Loop motor sequence frame extent');
    const events=[],channelsByFrame=new Set();
    for(let index=0;index<MOTOR_EVENT_CAPACITY;index++){
      const row=offset+44+index*16;
      if(index>=eventCount){
        if(!bytes.subarray(row,row+16).every(value=>value===0))
          throw new Error('Loop motor unused event capacity is not zero');
        continue;
      }
      const event={sourceFrame:u32(row),channel:u32(row+4),command:u32(row+8),ordinal:u32(row+12)};
      if(event.sourceFrame<firstFrame||event.sourceFrame>=firstFrame+span||
         event.channel>3||event.command>2||event.ordinal!==index||
         index&&event.sourceFrame<events[index-1].sourceFrame)
        throw new Error('Loop motor event frame/channel/command/order');
      const channelKey=`${event.sourceFrame}/${event.channel}`;
      if(channelsByFrame.has(channelKey))throw new Error('Loop motor duplicate channel/frame');
      channelsByFrame.add(channelKey);
      events.push(event);
    }
    return {state,span,firstFrame,framesSeen,eventCount,token:u64(offset+20),world:u64(offset+28),
      contextEpoch:u64(offset+36),events};
  };
  const result={schema:u32(0),bytes:u32(4),mode:u32(8),selectedLane:u32(12),published:u32(16),
    commitState:u32(20),failure:u32(24),reserved:u32(28),baseline:sequence(32),selected:sequence(524),
    base64:bytes.toString('base64')};
  if(result.schema!==1||result.bytes!==CPU_LOOP_MOTOR_EVIDENCE_BYTES||result.mode!==1||
     ![0,1,2,3].includes(result.selectedLane)||result.published!==0||
     ![0,1,2].includes(result.commitState)||result.reserved!==0)
    throw new Error('Loop motor schema/mode/publication/commit state');
  return result;
}

export function hashCpuCorrectedLoopMotorEvents(events){
  let hash=0xcbf29ce484222325n;
  for(const event of events)for(const key of ['sourceFrame','channel','command','ordinal']){
    const value=event[key];
    if(!Number.isInteger(value)||value<0||value>0xffffffff)throw new Error('Loop motor hash event u32');
    for(let byte=0;byte<4;byte++){
      hash^=BigInt((value>>>(byte*8))&255);
      hash=BigInt.asUintN(64,hash*0x100000001b3n);
    }
  }
  return hash.toString();
}

export function requireCpuCorrectedLoopMotorEvidence(value,{preFrame,span,selectedLane,baseline=null}={}){
  if(value?.failure!==0||value.commitState!==0||value.published!==0||value.selectedLane!==selectedLane)
    throw new Error('Loop motor pending capture-only selection refused');
  for(const sequence of [value.baseline,value.selected]){
    if(sequence.state!==1||sequence.framesSeen!==span||sequence.span!==span||sequence.firstFrame!==preFrame||
       sequence.token==='0'||sequence.world==='0'||sequence.contextEpoch==='0')
      throw new Error('Loop motor complete local sequence identity/extent');
  }
  for(const key of motorIdentityKeys)if(value.baseline[key]!==value.selected[key])
    throw new Error(`Loop motor local selected identity ${key} differs`);
  if(selectedLane===CPU_LOOP_MOTOR_LANE.baseline&&
     JSON.stringify(value.baseline)!==JSON.stringify(value.selected))
    throw new Error('Loop motor baseline selection differs from baseline journal');
  if(baseline&&JSON.stringify(value.baseline)!==JSON.stringify(baseline))
    throw new Error('Loop motor baseline journal changed after restore');
  return value;
}

/** Local receipt identities authenticate each journal; only source events cross instances. */
export function compareCpuCorrectedLoopMotorEvents(reference,selected){
  for(const sequence of [reference,selected])if(!sequence||sequence.state!==1||
     sequence.framesSeen!==sequence.span||sequence.events.length!==sequence.eventCount)
    throw new Error('Loop motor comparison requires complete selected journals');
  if(JSON.stringify(reference.events)!==JSON.stringify(selected.events)||
     reference.eventCount!==selected.eventCount||reference.firstFrame!==selected.firstFrame||
     reference.span!==selected.span||reference.framesSeen!==selected.framesSeen)
    throw new Error('Loop motor selected actual-authority events differ');
  return {equal:true,eventCount:reference.eventCount,firstFrame:reference.firstFrame,span:reference.span,
    eventHash:hashCpuCorrectedLoopMotorEvents(reference.events),published:false,mode:'capture-only'};
}

export function decodeCpuCorrectedLoopMotorCommitReceipt(input,selected,selectedLane){
  const bytes=Buffer.from(input);
  if(bytes.length!==CPU_LOOP_MOTOR_RECEIPT_BYTES)throw new Error('Loop motor commit receipt extent');
  const u32=offset=>bytes.readUInt32LE(offset);
  const u64=offset=>(BigInt(u32(offset))|(BigInt(u32(offset+4))<<32n)).toString();
  const result={schema:u32(0),bytes:u32(4),status:u32(8),selectedLane:u32(12),published:u32(16),
    eventCount:u32(20),firstFrame:u32(24),span:u32(28),token:u64(32),world:u64(40),
    contextEpoch:u64(48),eventHash:u64(56)};
  if(result.schema!==1||result.bytes!==CPU_LOOP_MOTOR_RECEIPT_BYTES||result.status!==1||
     result.selectedLane!==selectedLane||result.published!==0||
     result.eventCount!==selected?.eventCount||result.eventHash!==hashCpuCorrectedLoopMotorEvents(selected.events))
    throw new Error('Loop motor commit receipt status/selection/hash/publication');
  for(const key of motorIdentityKeys)if(result[key]!==selected[key])
    throw new Error(`Loop motor commit receipt local identity ${key} differs`);
  return result;
}

/** Shared production boundary: one awaited immutable sink, then checked retirement. */
export async function persistCpuCorrectedLoopInterval(index,interval,summary,sink,retire){
  if(interval.reference?.motorEvidence||interval.predicted?.motorEvidence){
    interval.comparisons??={};
    interval.comparisons.motor=compareCpuCorrectedLoopMotorEvents(
      interval.reference.motorEvidence.selected,interval.predicted.motorEvidence.selected);
  }
  const receipt=validateSinkReceipt(await sink(index,interval));
  interval.evidenceReceipt=receipt;
  summary.evidence=receipt;summary.status='evidence-saved';
  const retirement=await retire();
  summary.retirement=retirement;
  interval.status='complete';summary.status='complete';
  return receipt;
}

function checkActionAndMotionDivergence(referenceCpu,predictedCpu,referenceRows,predictedRows,index){
  const action=recordPredictedActionDivergence(referenceCpu,predictedCpu);
  const motionDifferent=referenceRows.some((row,i)=>
    row.boundary.players[1].motion!==predictedRows[i].boundary.players[1].motion);
  if(!motionDifferent)throw new Error(`Speculative capsule ${index} did not change actual remote motion`);
  return {action,motionDifferent:true};
}

export async function runBrowserCpuCorrectedLoop(predictedPage,referencePage,wire,checkpoint,
  expectedInputMapping,stocks,previousFrame,beforeDiagnostics,onIntervalEvidence,{packetInput=null}={}){
  if(typeof onIntervalEvidence!=='function')throw new Error('Corrected loop requires its interval evidence sink');
  const mapping=validateSourceInputMapping(expectedInputMapping,beforeDiagnostics.slotCount);
  const localPort=mapping[0].rawController,remotePort=mapping[1].rawController;
  if(localPort!==0||remotePort!==1||!Array.isArray(stocks)||stocks.length!==2||stocks.some(value=>value!==4))
    throw new Error('Corrected loop requires source ports 0/1 and four stocks each');
  const initial=decode(checkpoint.pad);
  if(initial.length!==FRAME_BYTES||!padSlot(initial,remotePort).equals(decode(INITIAL_REMOTE_PAD)))
    throw new Error('Corrected loop initial remote PAD differs from the explicit neutral seed');
  const recipe=buildCpuCorrectedLoopCapsules(localPort,remotePort);
  const schedule=createCpuCorrectedLoopSchedule(localPort,remotePort);
  const report={schema:'independent-browser-corrected-confirmed-loop-v1',status:'running',
    startSample:300,endSample:343,capsules:[],timeline:{persistent:true,initialRemotePadBase64:INITIAL_REMOTE_PAD,
      maxPredictionTicks:7,confirmedBoundary:true},
    scope:packetInput?
      'eight contiguous one-capsule intervals on two independently initialized pages; Node-held local relay PAD receive authority for 44 rows; no browser-owned sockets or rolling multi-checkpoint admission':
      'eight contiguous one-capsule intervals on two independently initialized pages; no live relay or rolling multi-checkpoint admission'};
  const defaults=correctedPageDefaults(mapping,stocks),gpu=new Map([[referencePage,previousFrame],[predictedPage,previousFrame]]);
  const diagnostics=new Map([[referencePage,beforeDiagnostics],[predictedPage,beforeDiagnostics]]);
  let loopEnabled=new Set(),current=null,sinkAttempted=false;
  const laneFor=(interval,page)=>page===referencePage?interval.reference:interval.predicted;
  const operate=async(interval,page,command,options={})=>{
    const result=await page.evaluate(operateCorrectedInPage,{...defaults,readLoopMotorEvidence:true,...options,command});
    const lane=laneFor(interval,page);
    lane.lastOperationResult=result;
    if(command===53){
      const metadata=decodeCpuCorrectedLoopMetadata(decode(result.loopEvidenceBase64??''));
      const motorCommit=decodeCpuCorrectedLoopMotorCommitReceipt(decode(result.motorCommitReceiptBase64??''),
        lane.motorEvidence.selected,lane.motorEvidence.selectedLane);
      lane.motorCommitReceipt=motorCommit;
      lane.commands.push({command,result:0,loopMetadata:metadata,motorCommit,timing:result.timing});
      return {result,metadata,motorCommit,timing:result.timing};
    }
    const evidence=decodeCpuCorrectedLoopEvidence(decode(result.evidenceBase64),interval.span);
    validateCpuCorrectedLoopCommandEvidence(evidence,command,{preFrame:interval.preFrame,span:interval.span});
    lane.lastNativeEvidence=evidence;lane.commands.push({command,result:0,state:evidence.state,
      baselineStepNoDrawMask:evidence.baselineStepNoDrawMask,replayStepNoDrawMask:evidence.replayStepNoDrawMask,
      timing:result.timing});
    if(options.readLoopEvidence){
      result.loopMetadata=decodeCpuCorrectedLoopMetadata(decode(result.loopEvidenceBase64??''));
    }
    if(options.draw){
      const before=gpu.get(page),frame=validateAuroraFrame(result.status,before.beginFrameId,before.endFrameId);
      const next=decodeDrawDiagnostics(decode(result.drawDiagnosticsBase64));
      const selected=validateSelectedDrawPath(diagnostics.get(page),next,result.boundary.players,mapping);
      lane.draws.push({command,gpu:result.status,selected});gpu.set(page,frame);diagnostics.set(page,next);
    }
    return {result,evidence};
  };
  const endpoint=async(interval,page,view,provenance,name)=>{
    const value=decodeCpuParticleEndpointResult(await page.evaluate(collectCpuEndpointInPage,
      {view,provenance,particleCellEndpoint:true}));
    laneFor(interval,page).endpoints[name]=value;return value;
  };
  const metadataExpected=(index,capsule,preFrame,retired)=>({windowsCaptured:index+1,
    windowsRetired:index+(retired?1:0),activeCapsules:retired?0:1,maxActiveCapsules:1,
    windowFirstSample:capsule.startSample,windowPreFrame:preFrame,
    windowPostFrame:preFrame+capsule.span,windowSpan:capsule.span});

  try{
    let expectedSourceFrame=checkpoint.boundary.sourceFrame;
    if(!Number.isSafeInteger(expectedSourceFrame)||expectedSourceFrame<0)
      throw new Error('Corrected loop checkpoint source frame is invalid');
    for(const [index,capsule] of recipe.entries()){
      const interval={status:'running',index,startSample:capsule.startSample,span:capsule.span,
        kind:capsule.kind,preFrame:expectedSourceFrame,postFrame:expectedSourceFrame+capsule.span,
        inputs:[],reference:{commands:[],draws:[],rows:[],endpoints:{}},
        predicted:{commands:[],draws:[],rows:[],endpoints:{}},comparisons:{}};
      current=interval;sinkAttempted=false;
      if(packetInput){
        packetInput.prepareCapsule(capsule);
        if(!capsule.kind.startsWith('speculative')){
          await packetInput.publishCapsule(capsule);
          interval.packetAuthority=await packetInput.drainCapsule(capsule);
        }
      }
      report.capsules.push({index,startSample:capsule.startSample,span:capsule.span,kind:capsule.kind,status:'running'});
      const referenceCapture=await operate(interval,referencePage,32,{wire,
        loopEnableSchema:loopEnabled.has(referencePage)?undefined:CPU_CORRECTED_LOOP_SCHEMA,
        selectSpan:capsule.span,readLoopEvidence:true});
      loopEnabled.add(referencePage);
      const predictedCapture=await operate(interval,predictedPage,32,{wire,
        loopEnableSchema:loopEnabled.has(predictedPage)?undefined:CPU_CORRECTED_LOOP_SCHEMA,
        selectSpan:capsule.span,readLoopEvidence:true});
      loopEnabled.add(predictedPage);
      requireCpuCorrectedLoopMetadata(referenceCapture.result.loopMetadata,
        metadataExpected(index,capsule,interval.preFrame,false),`capsule${index} reference capture`);
      requireCpuCorrectedLoopMetadata(predictedCapture.result.loopMetadata,
        metadataExpected(index,capsule,interval.preFrame,false),`capsule${index} predicted capture`);
      if(JSON.stringify(referenceCapture.result.loopMetadata)!==JSON.stringify(predictedCapture.result.loopMetadata))
        throw new Error(`Corrected loop capsule ${index} capture metadata differs across pages`);
      const refPre=await endpoint(interval,referencePage,CPU_ENDPOINT_VIEW.pre,CPU_ENDPOINT_PROVENANCE.sharedPre,'pre');
      const predPre=await endpoint(interval,predictedPage,CPU_ENDPOINT_VIEW.pre,CPU_ENDPOINT_PROVENANCE.sharedPre,'pre');
      interval.comparisons.pre=compareCpuReferenceParticleEndpoints(refPre,predPre);
      interval.baselineRaw=[];
      for(let i=0;i<capsule.span;i++){
        const sample=capsule.startSample+i,literal=capsule.rows[i];
        const speculative=capsule.kind.startsWith('speculative');
        let referencePad=literal,remotePad=null;
        if(packetInput&&!speculative){
          remotePad=packetInput.getRemotePad(sample);
          const receivedReference=validateLiteralPad(literal,`reference sample ${sample}`);
          receivedReference.set(remotePad,remotePort*PAD_BYTES);
          if(!equal(receivedReference,decode(literal)))
            throw new Error(`Received A2 PAD contribution differs from the immutable reference at ${sample}`);
          referencePad=encode(receivedReference);
        }
        const plan=planCpuCorrectedLoopForward(schedule,sample,referencePad,{speculative,remotePad});
        const predictedPad=encode(plan.frame);
        const ref=await operate(interval,referencePage,33+i,{pad:referencePad,draw:i<capsule.span-1});
        const pred=await operate(interval,predictedPage,33+i,{pad:predictedPad,draw:i<capsule.span-1});
        const refPair=ref.evidence.tickPairs[i],predPair=pred.evidence.tickPairs[i];
        if(ref.result.boundary.sourceFrame!==interval.preFrame+i+1||
           pred.result.boundary.sourceFrame!==interval.preFrame+i+1||
           !equal(decode(refPair.baselinePad),decode(referencePad))||
           !equal(decode(predPair.baselinePad),decode(predictedPad)))
          throw new Error(`Corrected loop source frame/literal PAD differs at sample ${sample}`);
        const refRaw=rawFive(refPair,'baseline'),predRaw=rawFive(predPair,'baseline');
        interval.baselineRaw.push({sample,reference:refRaw,predicted:predRaw});
        if(!capsule.kind.startsWith('speculative'))
          compareCpuCorrectedLoopRawFive(refRaw,predRaw,`confirmed sample ${sample}`);
        interval.reference.rows.push({sample,pad44Base64:referencePad,boundary:ref.result.boundary});
        interval.predicted.rows.push({sample,pad44Base64:predictedPad,predictedPad44Base64:predictedPad,
          boundary:pred.result.boundary});
        interval.inputs.push({sample,pad44Base64:literal,predictedPad44Base64:predictedPad,
          speculativeTicks:plan.speculative_ticks});
        acknowledgeCpuCorrectedLoopForward(schedule,plan);
      }
      const refFinalStep=interval.reference.commands.at(-1),predFinalStep=interval.predicted.commands.at(-1);
      if(refFinalStep?.command!==32+capsule.span||predFinalStep?.command!==32+capsule.span)
        throw new Error(`Corrected loop baseline span ${capsule.span} did not finish at its exact command`);
      const refCpu=await endpoint(interval,referencePage,CPU_ENDPOINT_VIEW.cpuEnd,CPU_ENDPOINT_PROVENANCE.reference,'cpu');
      const predBaselineCpu=await endpoint(interval,predictedPage,CPU_ENDPOINT_VIEW.cpuEnd,
        CPU_ENDPOINT_PROVENANCE.reference,capsule.kind.startsWith('speculative')?'speculativeCpu':'cpu');
      validateCpuParticleEndpointAgainstPre(refCpu,refPre);
      validateCpuParticleEndpointAgainstPre(predBaselineCpu,predPre);
      if(!capsule.kind.startsWith('speculative'))
        interval.comparisons.cpu=compareCpuReferenceParticleEndpoints(refCpu,predBaselineCpu,
          {preLeft:refPre,preRight:predPre});
      await operate(interval,referencePage,40,{draw:true});
      if(packetInput&&capsule.kind.startsWith('speculative')){
        const predictedDrawRequestNs=process.hrtime.bigint();
        const predictedDrawPromise=operate(interval,predictedPage,40,{draw:true}).then(result=>{
          packetInput.notePredictedDrawResolved(capsule.startSample);
          return result;
        });
        // Start the browser command first, then release actual peer input without
        // awaiting either operation; both are outstanding in this Node turn.
        const packetPublishPromise=packetInput.publishCapsule(capsule,
          {predictedDrawRequestNs:predictedDrawRequestNs.toString()});
        const [drawResult,packetResult]=await Promise.allSettled([predictedDrawPromise,packetPublishPromise]);
        if(drawResult.status==='rejected')throw drawResult.reason;
        if(packetResult.status==='rejected')throw packetResult.reason;
        interval.packetAuthority=packetResult.value;
      }else await operate(interval,predictedPage,40,{draw:true});
      const refIdle=await operate(interval,referencePage,41);
      const predIdle=await operate(interval,predictedPage,41);
      interval.reference.nativeEvidence=refIdle.evidence;
      interval.predicted.nativeEvidence=predIdle.evidence;
      for(const [lane,operation] of [[interval.reference,refIdle],[interval.predicted,predIdle]]){
        lane.motorEvidence=decodeCpuCorrectedLoopMotorEvidence(decode(operation.result.motorEvidenceBase64??''));
        requireCpuCorrectedLoopMotorEvidence(lane.motorEvidence,{preFrame:interval.preFrame,span:interval.span,
          selectedLane:CPU_LOOP_MOTOR_LANE.baseline});
        lane.motorBaselineEvidence=lane.motorEvidence;
      }
      const refDraw=await endpoint(interval,referencePage,CPU_ENDPOINT_VIEW.drawEnd,CPU_ENDPOINT_PROVENANCE.reference,'draw');
      const predBaselineDraw=await endpoint(interval,predictedPage,CPU_ENDPOINT_VIEW.drawEnd,
        CPU_ENDPOINT_PROVENANCE.reference,capsule.kind.startsWith('speculative')?'speculativeDraw':'draw');
      validateCpuParticleEndpointAgainstPre(refDraw,refPre);
      validateCpuParticleEndpointAgainstPre(predBaselineDraw,predPre);
      interval.reference.endpoints.draw=refDraw;
      interval.predicted.endpoints.draw=predBaselineDraw;
      if(packetInput&&capsule.kind.startsWith('speculative'))
        interval.packetAuthority=await packetInput.drainCapsule(capsule);
      if(capsule.kind.startsWith('speculative')){
        interval.speculativeDivergence=checkActionAndMotionDivergence(refCpu,predBaselineCpu,
          interval.reference.rows,interval.predicted.rows,index);
      }else{
        interval.reference.endpoints.cpu=refCpu;interval.predicted.endpoints.cpu=predBaselineCpu;
        interval.comparisons.draw=compareCpuReferenceParticleEndpoints(refDraw,predBaselineDraw,
          {preLeft:refPre,preRight:predPre});
        interval.comparisons.drawRaw=compareCpuCorrectedLoopRawFive(
          rawFive(refIdle.evidence.drawnPair,'baseline'),rawFive(predIdle.evidence.drawnPair,'baseline'),
          `confirmed capsule ${index} final draw`);
      }
      if(capsule.kind.startsWith('speculative')){
        const actualRows=interval.reference.rows.map(row=>{
          const actual=validateLiteralPad(row.pad44Base64,`actual sample ${row.sample}`);
          if(packetInput){
            const remote=packetInput.getRemotePad(row.sample);
            actual.set(remote,remotePort*PAD_BYTES);
            if(!equal(actual,decode(capsule.rows[row.sample-capsule.startSample])))
              throw new Error(`Received A2 remote input does not reproduce the immutable PAD at ${row.sample}`);
          }
          return {sample:row.sample,pad44Base64:encode(actual)};
        });
        const baselineRows=interval.predicted.rows.map(row=>({tick:row.sample,predictedPad44Base64:row.predictedPad44Base64}));
        const correction=planCpuCorrectedLoopReplay(schedule,baselineRows,actualRows,capsule.startSample);
        interval.correction={earliestChanged:correction.earliestChanged,dirtyFrom:correction.dirtyFrom,
          through:correction.through,frames:correction.frames.map(row=>({tick:row.tick,
            frameBase64:encode(row.frame),unchangedPrefix:row.unchangedPrefix}))};
        await operate(interval,predictedPage,42);
        interval.replayRaw=[];
        for(let i=0;i<capsule.span;i++){
          const frame=correction.frames[i],command=43+i;
          const replay=await operate(interval,predictedPage,command,{pad:encode(frame.frame)});
          const refPair=refIdle.evidence.tickPairs[i],replayPair=replay.evidence.tickPairs[i];
          if(replay.result.boundary.sourceFrame!==interval.preFrame+i+1||
             !equal(decode(replayPair.replayPad),frame.frame))
            throw new Error(`Corrected loop replay source frame/PAD differs at ${frame.tick}`);
          const refRaw=rawFive(refPair,'baseline'),replayRaw=rawFive(replayPair,'replay');
          interval.replayRaw.push({sample:frame.tick,reference:refRaw,corrected:replayRaw});
          compareCpuCorrectedLoopRawFive(refRaw,replayRaw,`corrected sample ${frame.tick}`);
        }
        const correctedCpu=await endpoint(interval,predictedPage,CPU_ENDPOINT_VIEW.cpuEnd,
          CPU_ENDPOINT_PROVENANCE.corrected,'cpu');
        interval.predicted.endpoints.cpu=correctedCpu;
        validateCpuParticleEndpointAgainstPre(correctedCpu,predPre);
        interval.comparisons.cpu=compareCpuParticleEndpoints(refCpu,correctedCpu,
          {preLeft:refPre,preRight:predPre});
        await operate(interval,predictedPage,50,{draw:true});
        const correctedIdle=await operate(interval,predictedPage,51);
        interval.predicted.nativeEvidence=correctedIdle.evidence;
        interval.predicted.motorEvidence=decodeCpuCorrectedLoopMotorEvidence(
          decode(correctedIdle.result.motorEvidenceBase64??''));
        requireCpuCorrectedLoopMotorEvidence(interval.predicted.motorEvidence,
          {preFrame:interval.preFrame,span:interval.span,selectedLane:CPU_LOOP_MOTOR_LANE.corrected,
            baseline:interval.predicted.motorBaselineEvidence.baseline});
        const correctedDraw=await endpoint(interval,predictedPage,CPU_ENDPOINT_VIEW.drawEnd,
          CPU_ENDPOINT_PROVENANCE.corrected,'draw');
        interval.predicted.endpoints.draw=correctedDraw;
        validateCpuParticleEndpointAgainstPre(correctedDraw,predPre);
        interval.comparisons.draw=compareCpuParticleEndpoints(refDraw,correctedDraw,
          {preLeft:refPre,preRight:predPre});
        interval.comparisons.drawRaw=compareCpuCorrectedLoopRawFive(
          rawFive(refIdle.evidence.drawnPair,'baseline'),rawFive(correctedIdle.evidence.drawnPair,'replay'),
          `speculative capsule ${index} corrected final draw`);
        schedule.timeline.commitReplay(correction.replay);
        schedule.lastRemotePad=padSlot(validateLiteralPad(actualRows.at(-1).pad44Base64,'confirmed suffix'),remotePort);
        interval.timelineAfterReplay=schedule.timeline.status();
      }
      if(refIdle.result.boundary.sourceFrame!==interval.postFrame||
         predIdle.result.boundary.sourceFrame!==interval.postFrame)
        throw new Error(`Corrected loop capsule ${index} final baseline source frame drift`);
      interval.status='compared';
      // Evidence is immutable before command53 retires native ownership.
      const summary=report.capsules[index];
      if(packetInput)summary.packetAuthority={...interval.packetAuthority};
      await persistCpuCorrectedLoopInterval(index,interval,summary,async(sinkIndex,sinkInterval)=>{
        sinkAttempted=true;return onIntervalEvidence(sinkIndex,sinkInterval);
      },async()=>{
        const refRetire=await operate(interval,referencePage,53,{readLoopEvidence:true});
        // Preserve progress if the other page refuses retirement.
        summary.retirementProgress={reference:refRetire.metadata,referenceMotorCommit:refRetire.motorCommit};
        const predRetire=await operate(interval,predictedPage,53,{readLoopEvidence:true});
        summary.retirementProgress.predicted=predRetire.metadata;
        summary.retirementProgress.predictedMotorCommit=predRetire.motorCommit;
        const retiredExpected=metadataExpected(index,capsule,interval.preFrame,true);
        requireCpuCorrectedLoopMetadata(refRetire.metadata,retiredExpected,`capsule${index} reference retirement`);
        requireCpuCorrectedLoopMetadata(predRetire.metadata,retiredExpected,`capsule${index} predicted retirement`);
        if(JSON.stringify(refRetire.metadata)!==JSON.stringify(predRetire.metadata))
          throw new Error(`Corrected loop capsule ${index} retirement metadata differs across pages`);
        // Keep command durations in the mutable run summary after the immutable
        // interval sink has recorded evidence. This includes command53 retirement
        // without changing the pre-retirement evidence receipt.
        summary.commandTimings={reference:interval.reference.commands.map(({command,timing})=>({command,timing})),
          predicted:interval.predicted.commands.map(({command,timing})=>({command,timing}))};
        return {reference:refRetire.metadata,predicted:predRetire.metadata,
          motorCommit:{reference:refRetire.motorCommit,predicted:predRetire.motorCommit}};
      });
      summary.timeline=schedule.timeline.status();
      report.timelineAfterCapsule=schedule.timeline.status();
      expectedSourceFrame=interval.postFrame;
      current=null;sinkAttempted=false;
    }
    const timeline=schedule.timeline.status();
    if(timeline.simulated_through!==343||timeline.confirmed_through!==343||
       timeline.remote_contiguous_through!==343||timeline.dirty_from!==null||timeline.plan_outstanding)
      throw new Error(`Corrected loop did not finish at confirmed sample343: ${JSON.stringify(timeline)}`);
    if(report.capsules.length!==8||report.capsules.some(row=>row.status!=='complete'))
      throw new Error('Corrected loop did not complete and retire all eight capsules');
    report.finalTimeline=timeline;
    if(packetInput){
      const requiredIntervals=recipe.filter(row=>row.kind.startsWith('speculative')).length;
      const overlapIntervals=report.capsules.filter(row=>row.packetAuthority?.releasedWithPredictedCommand40===true);
      report.packetCommand40Overlap={requiredSpeculativeIntervals:requiredIntervals,
        observedSpeculativeIntervals:overlapIntervals.length,
        intervals:overlapIntervals.map(row=>({index:row.index,startSample:row.startSample,
          endSample:row.endSample,acceptedReceiveEvents:row.packetAuthority.receiveEvents.length,
          predictedCommand40ReceiveCallbackOverlap:row.packetAuthority.predictedCommand40ReceiveCallbackOverlap,
          overlapEvidence:row.packetAuthority.overlapEvidence})),
        status:overlapIntervals.length===requiredIntervals&&overlapIntervals.some(row=>
          row.packetAuthority.predictedCommand40ReceiveCallbackOverlap===true)?'passed':'failed'};
      if(report.packetCommand40Overlap.status!=='passed')
        throw new Error('Packet-backed corrected loop did not observe an accepted remote receive callback overlap during predicted command 40 in either speculative interval');
    }
    report.status='passed';return report;
  }catch(error){
    report.status='failed';report.failure=String(error);report.timelineFailure=schedule.timeline.status();
    for(const [name,page] of [['reference',referencePage],['predicted',predictedPage]]){
      try{report[`${name}Partial`]=await page.evaluate(()=>window.__slippiSynctest?.cpuCorrectedProgress??null);}
      catch(readError){report[`${name}PartialReadError`]=String(readError);}
    }
    if(current)current.sourceProgress={reference:report.referencePartial,predicted:report.predictedPartial};
    if(current){
      current.status='failed';current.failure=String(error);
      // Endpoint/source/raw data must remain reachable even when the sink itself fails.
      report.partialInterval=current;
      if(!sinkAttempted){
        sinkAttempted=true;
        try{current.evidenceReceipt=validateSinkReceipt(await onIntervalEvidence(current.index,current));
          report.capsules[current.index].evidence=current.evidenceReceipt;}
        catch(sinkError){current.evidenceSinkFailure=String(sinkError);}
      }
      const summary=report.capsules[current.index];
      if(summary)summary.status=current.evidenceReceipt?'failed-after-evidence':'failed';
      current=null;
    }
    error.cpuCorrectedLoopPartial=report;throw error;
  }finally{
    schedule.timeline.dispose();
  }
}
