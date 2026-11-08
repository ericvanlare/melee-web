// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import test from 'node:test';
import {MatchInputTimeline} from '../../../scripts/net_rollback_input_timeline.mjs';
import {createRuntimeMatchSessionLoop} from '../runtime_match_session_loop.mjs';

function ownerFixture({pendingFrames=[], dirtyAtPending=false, terminalAtRefill=false}={}){
  const neutral=Array(11).fill(0), changed=[0,0,80,...Array(8).fill(0)];
  const timeline=new MatchInputTimeline({localPort:1,remotePort:0,initialRemotePad:neutral});
  const calls=[], received=new Map(Array.from({length:5},(_,tick)=>[tick,neutral]));
  let frame=0,preFrame=0,phase=4,presentationSerial=0,loop;
  const owner={
    status:()=>({metadata:{currentFrame:frame,preFrame,phase}}),
    pacingStart(){}, pacingJournal:()=>[], pacingWaitJournal:()=>[], journal:()=>[],
    pacingSample:budget=>({steps:budget,currentFrame:frame,pendingSteps:1}),
    admitEndpoint:()=>({}),
    presentation:()=>({sourceFrameAfter:frame,presentationSerial:String(presentationSerial)}),
    checksum:()=>Array(64).fill(0),
    sourceRefillBoundary(expected){
      assert.equal(expected,frame);
      return pendingFrames.includes(frame)?'owned-pending':'quiet';
    },
  };
  const peer={async call(operation,value){
    switch(operation){
    case 'arenaEnable': case 'inlineEnable': case 'episodePublish': return {};
    case 'episodeTimeline': return timeline.status();
    case 'peerRemoteSnapshot': return received.get(value.tick)??null;
    case 'episodeAdmit':
      calls.push(['admit',value.tick,frame]);
      return {admission:timeline.acceptRemote(value.tick,received.get(value.tick))};
    case 'episodePoll': return {ticket:{ticketId:value.tick+1}};
    case 'episodeForward': {
      const {tick,actual}=value;
      calls.push(['forward',tick,actual]);
      assert.equal(timeline.recordLocal(tick,neutral).status,'accepted');
      const plan=timeline.planForward();
      assert.equal(plan.remote_source==='actual',actual);
      timeline.commitForward(plan);
      if(tick===6&&terminalAtRefill)return {terminal:{selectedOrdinal:7}};
      frame++;phase=21;
      if(frame===6&&pendingFrames.includes(6)){
        received.set(5,changed);received.set(6,neutral);
        if(dirtyAtPending)timeline.acceptRemote(5,changed);
      }
      return {};
    }
    case 'episodeAwaitNetworkInput':
      calls.push(['wait',value.kind,value.tick,value.requiredThrough,frame]);
      received.set(value.tick,neutral);
      return {};
    case 'episodeReplay': {
      calls.push(['replay',value.dirty,frame]);
      const plan=timeline.planReplay();
      timeline.commitReplay(plan);
      return {rows:[],plan};
    }
    case 'episodeRebase':
      calls.push(['rebase',value]);
      preFrame=value;
      loop.abortLateScenario();
      return {};
    case 'command':
      if(value==='recordingPresent'||value==='present')presentationSerial++;
      if(value==='sealPresentedFrontier'){
        assert(!pendingFrames.includes(frame),'attempted to seal a pending refill');
        calls.push(['seal',frame]);
      }
      return owner.status();
    default: throw Error(`Unhandled modeled operation ${operation}`);
    }
  }};
  return {owner,peer,calls,start(options={}){
    loop=createRuntimeMatchSessionLoop({owner,peer,role:'beta',inputTicks:12,
      streaming:true,matchFinal:true,networkBackpressure:true,reserveHpsTick:true,...options});
    return loop;
  }};
}

async function runFixture(t,options){
  const previous=globalThis.requestAnimationFrame;
  globalThis.requestAnimationFrame=callback=>setImmediate(()=>callback(performance.now()));
  t.after(()=>{globalThis.requestAnimationFrame=previous;});
  const fixture=ownerFixture(options),loop=fixture.start();
  await loop.prepare();
  return {result:await loop.run(),calls:fixture.calls};
}

test('retained peer bytes enter the timeline in order after one natural refill tick',async t=>{
  const {result,calls}=await runFixture(t,{pendingFrames:[6]});
  assert.equal(result.result,'CANCELLED','fixture stops after the checked rebase');
  assert.deepEqual(calls.slice(-6),[
    ['forward',6,false],['admit',5,7],['admit',6,7],['seal',7],['replay',5,7],['rebase',7],
  ]);
  assert.deepEqual(result.corrections[0].replay.plan.frames.map(({tick,remote_source,remote_pad})=>
    [tick,remote_source,remote_pad[2]]),[[5,'actual',80],[6,'actual',0]]);
  assert.deepEqual(result.hps_refills,{quiet_rebases:0,source_ticks:1,recent:[
    {source_frame:6,pre_frame:0,remote_next_before:5,settled_frame:7,remote_next_after:7,
      dirty_from_after_admission:5},
  ]});
});

test('quiet six-tick frontier waits for confirmation and rebases before the seventh poll',async t=>{
  const {result,calls}=await runFixture(t,{});
  assert.equal(result.result,'CANCELLED');
  assert.deepEqual(calls.slice(-4),[['wait',1,5,5,6],['admit',5,6],['seal',6],['rebase',6]]);
  assert(!calls.some(([operation,tick])=>operation==='forward'&&tick===6));
  assert.equal(result.hps_refills.quiet_rebases,1);
});

test('already-dirty and repeated-pending boundaries refuse without sealing',async t=>{
  const dirty=await runFixture(t,{pendingFrames:[6],dirtyAtPending:true});
  assert.match(dirty.result.failure.message,/no clean source tick/);
  assert(!dirty.calls.some(([operation,tick])=>operation==='seal'||operation==='forward'&&tick===6));
  const repeated=await runFixture(t,{pendingFrames:[6,7]});
  assert.match(repeated.result.failure.message,/did not settle on the next source tick/);
  assert(!repeated.calls.some(([operation,tick])=>operation==='seal'||operation==='forward'&&tick===7));
  const ending=await runFixture(t,{pendingFrames:[6],terminalAtRefill:true});
  assert.match(ending.result.failure.message,/crossed the terminal source boundary/);
  assert.equal(ending.result.hps_refills.source_ticks,0);
  assert(!ending.calls.some(([operation])=>operation==='seal'));
});

test('refill reservation refuses scripted late-input scenarios',()=>{
  const fixture=ownerFixture();
  assert.throws(()=>fixture.start({inputTicks:120,lateClassPiece:true}),/without scripted late inputs/);
});
