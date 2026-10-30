// SPDX-License-Identifier: GPL-3.0-or-later
// Input-only helpers for the private one-anchor history witness. Native owns
// every snapshot and acknowledgement; these helpers never execute source work.
import {MatchInputTimeline} from '../../../scripts/net_rollback_input_timeline.mjs';
import {CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44} from './cpu_corrected_two_window_runner.mjs';

export const CPU_HISTORY_FIRST_SAMPLE=300;
export const CPU_HISTORY_SAMPLE_COUNT=4;
const PAD_BYTES=11,FRAME_BYTES=44;
const equal=(a,b)=>Buffer.from(a).equals(Buffer.from(b));

export function buildCpuHistoryCorpus(){
  const neutral=Buffer.from(CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,'base64');
  // Authored PADStatus wire: button is big-endian u16, followed by signed
  // stick X/Y. First remote dash, then release, then X jump. Whether these
  // create distinct action selections is an assertion in the real capture.
  const dash=Buffer.from(neutral);dash[PAD_BYTES+2]=80;
  const jump=Buffer.from(neutral);jump.writeUInt16BE(0x0400,PAD_BYTES);
  return Object.freeze([dash,neutral,jump,neutral].map(row=>row.toString('base64')));
}

export function createCpuHistoryTimeline(){
  return new MatchInputTimeline({localPort:0,remotePort:1,startTick:CPU_HISTORY_FIRST_SAMPLE,
    initialRemotePad:new Uint8Array(PAD_BYTES),maxHistoryTicks:128,maxLookaheadTicks:8});
}

export function admitCpuHistoryRows(timeline,received,rows){
  if(timeline.status().plan_outstanding)
    throw new Error('History inbox must wait for native execution acknowledgement');
  for(const {tick,remotePad} of rows){
    const pad=Buffer.from(remotePad);
    if(pad.length!==PAD_BYTES)throw new Error('History receive requires literal 11-byte PAD');
    const old=received.get(tick);
    if(old&&!equal(old,pad))throw new Error('History receive conflicts with retained bytes');
    const result=timeline.acceptRemote(tick,pad);
    if(result.status!=='accepted')throw new Error(`History remote row ${tick} refused`);
    received.set(tick,Buffer.from(pad));
  }
}

export function planCpuHistoryReplay(timeline,selected,received,anchorSample){
  const before=timeline.status();
  if(before.plan_outstanding||before.dirty_from===null||!Number.isSafeInteger(anchorSample)||
     selected.length<1||selected.length>7||
     before.simulated_through!==anchorSample+selected.length-1||
     before.remote_contiguous_through!==before.simulated_through||
     before.dirty_from<anchorSample)
    throw new Error('History replay requires a confirmed dirty suffix inside one retained PRE');
  // Validate all retained bytes before asking the timeline to create a plan.
  // The prefix before dirty_from really was executed, and must stay identical.
  const expected=selected.map((row,index)=>{
    const tick=anchorSample+index,frame=Buffer.from(row.frame),remote=received.get(tick);
    if(row.tick!==tick||frame.length!==FRAME_BYTES||!remote||remote.length!==PAD_BYTES||
       frame[32]!==255||frame[43]!==255)
      throw new Error(`History selected frame ${tick} has no exact retained owner`);
    const actual=Buffer.from(frame);actual.set(remote,PAD_BYTES);
    if(tick<before.dirty_from&&!equal(frame,actual))
      throw new Error(`History unchanged prefix differs at ${tick}`);
    return actual;
  });
  const replay=timeline.planReplay();
  if(replay.kind!=='replay'||replay.from!==before.dirty_from||
     replay.through!==before.simulated_through||replay.speculative_ticks!==0||
     replay.frames.length!==replay.through-replay.from+1)
    throw new Error('History timeline dirty suffix drift');
  const frames=expected.map((frame,index)=>{
    const tick=anchorSample+index;
    if(tick>=replay.from){
      const row=replay.frames[tick-replay.from];
      if(row.tick!==tick||!equal(row.frame,frame)||
         !equal(row.local_pad,frame.subarray(0,PAD_BYTES))||
         !equal(row.remote_pad,frame.subarray(PAD_BYTES,2*PAD_BYTES)))
        throw new Error(`History timeline changed retained input ${tick}`);
    }
    return {tick,frame:Buffer.from(frame),unchangedPrefix:tick<replay.from};
  });
  return {anchorSample,replay,frames,dirtyFrom:replay.from,through:replay.through};
}
