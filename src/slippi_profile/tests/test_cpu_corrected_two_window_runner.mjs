// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import test from 'node:test';
import {
  CPU_CORRECTED_REUSE_EVIDENCE_BYTES, CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,
  buildCpuCorrectedTwoWindowFirstCorpus, buildCpuCorrectedTwoWindowSecondCorpus,
  decodeCpuCorrectedReuseEvidence, compareCpuCorrectedRawFive,
  planCpuCorrectedWindowForward, acknowledgeCpuCorrectedWindowForward,
  planCpuCorrectedWindowReplay,
} from './cpu_corrected_two_window_runner.mjs';
import {CPU_SEVEN_TICK_PAD44_CORPUS,createCpuSevenTickTimeline} from './cpu_seven_tick_runner.mjs';
import {validateCpuRestoreRouteSelection,validateBrowserProfileForRoute} from './run_write_census.mjs';
import {runBrowserSample} from './run_synctest.mjs';

const encode=value=>Buffer.from(value).toString('base64');
const decode=value=>Buffer.from(value,'base64');
const padSlot=(pad,port)=>pad.subarray(port*11,(port+1)*11);

test('two-window literal corpora preserve the legacy seven-tick corpus and disconnected ports',()=>{
  const legacy=[...CPU_SEVEN_TICK_PAD44_CORPUS];
  const first=buildCpuCorrectedTwoWindowFirstCorpus(),second=buildCpuCorrectedTwoWindowSecondCorpus();
  assert.deepEqual(CPU_SEVEN_TICK_PAD44_CORPUS,legacy,'historical one-window input remains unchanged');
  assert.equal(first.length,7);assert.equal(second.length,7);
  for(let index=0;index<7;index++){
    const old=decode(legacy[index]),a=decode(first[index]),b=decode(second[index]);
    assert.ok(padSlot(a,0).equals(Buffer.alloc(11)),'first window keeps P1 neutral');
    assert.ok(padSlot(a,1).equals(padSlot(old,1)),'first window preserves authored P2 S4 rows');
    assert.equal(a[32],0xff);assert.equal(a[43],0xff);
    assert.ok(padSlot(b,0).equals(Buffer.alloc(11)),'second window keeps P1 neutral');
    assert.equal(b[11],index===0?2:0,'second window is P2 B then neutral');
    assert.equal(b[32],0xff);assert.equal(b[43],0xff);
  }
  assert.equal(CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,
    'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=');
});

test('one timeline acknowledges first prediction, bridge, and second replay without a guard tick',()=>{
  const schedule=createCpuSevenTickTimeline(0,1,Buffer.alloc(11));
  const first=buildCpuCorrectedTwoWindowFirstCorpus(),second=buildCpuCorrectedTwoWindowSecondCorpus();
  const actualRows=[];
  for(let index=0;index<7;index++){
    const tick=300+index,plan=planCpuCorrectedWindowForward(schedule,tick,first[index]);
    assert.equal(plan.speculative_ticks,index+1);
    assert.ok(padSlot(Buffer.from(plan.frame),1).equals(Buffer.alloc(11)));
    actualRows.push({sample:tick,pad44Base64:first[index]});
    acknowledgeCpuCorrectedWindowForward(schedule,plan);
  }
  assert.equal(schedule.timeline.status().simulated_through,306);
  const firstReplay=planCpuCorrectedWindowReplay(schedule,actualRows,300);
  assert.equal(firstReplay.earliestChanged,300);
  schedule.timeline.commitReplay(firstReplay.replay);
  const bridgeTick=307,neutral=decode(CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44);
  schedule.timeline.recordLocal(bridgeTick,Buffer.alloc(11));
  assert.equal(schedule.timeline.acceptRemote(bridgeTick,Buffer.alloc(11)).status,'accepted');
  const bridgePlan=schedule.timeline.planForward();
  assert.equal(bridgePlan.speculative_ticks,0);assert.ok(Buffer.from(bridgePlan.frame).equals(neutral));
  schedule.timeline.commitForward(bridgePlan);
  assert.equal(schedule.timeline.status().simulated_through,307);
  const secondRows=[];
  for(let index=0;index<7;index++){
    const tick=308+index,plan=planCpuCorrectedWindowForward(schedule,tick,second[index]);
    assert.equal(plan.speculative_ticks,index+1);
    assert.ok(padSlot(Buffer.from(plan.frame),1).equals(Buffer.alloc(11)));
    secondRows.push({sample:tick,pad44Base64:second[index]});
    acknowledgeCpuCorrectedWindowForward(schedule,plan);
  }
  const secondReplay=planCpuCorrectedWindowReplay(schedule,secondRows,308);
  assert.equal(secondReplay.earliestChanged,308);
  assert.deepEqual(secondReplay.frames.map(row=>row.tick),[308,309,310,311,312,313,314]);
  assert.ok(secondReplay.frames.every((row,index)=>row.localPad.equals(Buffer.alloc(11))&&
    row.remotePad.equals(padSlot(decode(second[index]),1))));
  schedule.timeline.commitReplay(secondReplay.replay);
  const final=schedule.timeline.status();
  assert.equal(final.simulated_through,314);assert.equal(final.confirmed_through,314);
  assert.equal(final.remote_contiguous_through,314);assert.equal(final.dirty_from,null);
  assert.equal(final.plan_outstanding,false);
});

test('reuse evidence is fixed-width and validates retirement counters and window identity',()=>{
  const bytes=Buffer.alloc(CPU_CORRECTED_REUSE_EVIDENCE_BYTES);
  [1,40,2,2,0,1,308,184,191,0].forEach((value,index)=>bytes.writeUInt32LE(value,index*4));
  assert.deepEqual(decodeCpuCorrectedReuseEvidence(bytes),{schema:1,bytes:40,windowsCaptured:2,
    windowsRetired:2,activeCapsules:0,maxActiveCapsules:1,windowFirstSample:308,
    windowPreFrame:184,windowPostFrame:191,reserved:0});
  assert.throws(()=>decodeCpuCorrectedReuseEvidence(bytes.subarray(0,39)),/extent/);
  bytes.writeUInt32LE(1,36);assert.throws(()=>decodeCpuCorrectedReuseEvidence(bytes),/schema\/reserved/);
});

test('five bridge lanes compare exact bytes and reject a differing transfer byte',()=>{
  const lanes={observation:1280,pcm:4272,pad:44,rngProfile:32,transfer:40};
  const a=Object.fromEntries(Object.entries(lanes).map(([name,size])=>[name,encode(Buffer.alloc(size,7))]));
  const b=structuredClone(a);
  assert.equal(compareCpuCorrectedRawFive(a,b,'bridge').equal,true);
  const changed=Buffer.from(b.transfer,'base64');changed[39]^=1;b.transfer=encode(changed);
  assert.throws(()=>compareCpuCorrectedRawFive(a,b,'bridge tick 308'),/raw transfer differs at byte 39/);
});

test('two-window CLI is opt-in and requires the existing seven-tick v20 profile',()=>{
  const route={'--correct-predicted-branch-two-window':true,
    '--aggregate-cpu-drawn-reference-seven-tick':true,'--aggregate-cpu-restore-config':'config',
    '--slippi-lobj-presentation-cache':true,'--slippi-tobj-presentation-cache':true,
    '--slippi-particle-presentation-cache':true};
  assert.equal(validateCpuRestoreRouteSelection(route),'independent-browser-correction-two-window-v1');
  assert.throws(()=>validateCpuRestoreRouteSelection({'--correct-predicted-branch-two-window':true}),/fixed seven-tick/);
  assert.throws(()=>validateCpuRestoreRouteSelection({...route,'--correct-predicted-branch':true}),/mutually exclusive/);
  const profile={profile:'slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20'};
  const seven={evidence:true,evidenceBytes:true},reuse={enable:true,evidence:true,evidenceBytes:true};
  assert.equal(validateBrowserProfileForRoute(profile,false,null,true,true,true,true,seven,reuse),profile.profile);
  assert.throws(()=>validateBrowserProfileForRoute(profile,false,null,true,true,true,true,seven,
    {enable:true,evidence:true,evidenceBytes:false}),/reuse exports are unavailable/);
  assert.throws(()=>validateBrowserProfileForRoute(profile,false,null,true,true,true,false,null,reuse),
    /The v7 light owner requires two or seven ticks/);
});

test('two-window reuse validation follows predicted seven-tick page initialization',()=>{
  const source=readFileSync(new URL('./run_write_census.mjs',import.meta.url),'utf8');
  const referenceReuse=source.indexOf('const correctedReuseExports = correctedTwoWindow ? await page.evaluate');
  const referenceValidation=source.indexOf('validateBrowserProfileForRoute(init, twoTickDrawnReference',referenceReuse);
  const predictedCreate=source.indexOf('predictedPage=await context.newPage();',referenceValidation);
  const predictedInit=source.indexOf('predictedInit=await runBrowserInit(predictedPage,',predictedCreate);
  const predictedReuse=source.indexOf('const predictedReuseExports=await predictedPage.evaluate',predictedInit);
  const predictedValidation=source.indexOf('validateBrowserProfileForRoute(predictedInit,',predictedReuse);
  assert.ok(referenceReuse>=0&&referenceValidation>referenceReuse,'validate reference exports after reading them');
  assert.ok(predictedCreate>referenceValidation&&predictedInit>predictedCreate,
    'create and initialize the seven-tick predicted page before reading its exports');
  assert.ok(predictedReuse>predictedInit&&predictedValidation>predictedReuse,
    'validate predicted reuse exports only after its initialized module is available');
});

test('literal bridge passes the exact 44-byte PAD to raw step, then draws and copies all five lanes',async()=>{
  const heap=new Uint8Array(8192),view=new DataView(heap.buffer),calls=[];
  const padPointer=6400,drawPointer=6500,rngPointer=6700,transferPointer=6740;
  view.setUint32(0,176,true);view.setUint32(8,1,true);
  for(let player=0;player<2;player++){
    const base=72+192*player;view.setInt32(base+20,14,true);
    view.setUint32(base+136,player,true);view.setInt32(base+148,4,true);view.setInt32(base+152,42,true);
  }
  view.setUint32(drawPointer,6,true);heap.fill(0x39,2048,6320);heap.fill(0x52,rngPointer,rngPointer+32);
  heap.fill(0x63,transferPointer,transferPointer+40);
  const module={HEAPU8:heap,_melee_web_synctest_draw_decision:()=>1,
    _melee_web_snapshot_step_raw:()=>0,
    _melee_web_snapshot_input:()=>padPointer,_melee_web_snapshot_observation_size:()=>1280,
    _melee_web_snapshot_observation:()=>0,_melee_web_snapshot_pcm_size:()=>4272,
    _melee_web_snapshot_pcm:()=>2048,_melee_web_synctest_draw_diagnostics_size:()=>132,
    _melee_web_synctest_draw_diagnostics:()=>drawPointer,_melee_web_snapshot_rng_profile_observation_size:()=>32,
    _melee_web_snapshot_rng_profile_observation:()=>rngPointer,_melee_web_snapshot_transfer_diagnostic_size:()=>40,
    _melee_web_snapshot_transfer_diagnostic:()=>transferPointer,_melee_web_snapshot_error:()=>1,
    _melee_web_synctest_motion_boundary:()=>0,_melee_web_synctest_is_mario_kind:()=>1,
    _melee_web_synctest_submission_json:()=>2,UTF8ToString:()=>JSON.stringify({rendererErrorCount:0,
      pendingFramePackets:0,pendingStagingBuffers:0,workerBusy:0,drawCallCount:1,lastBeginFrameId:2,lastEndFrameId:2}),
    async ccall(identifier,returnType,argTypes,args,options){
      assert.equal(returnType,'number');assert.deepEqual(options,{async:true});calls.push(identifier);
      if(identifier==='melee_web_snapshot_step_raw'){
        assert.deepEqual(argTypes,[]);assert.deepEqual(args,[]);view.setUint32(0,177,true);
      }else if(identifier==='melee_web_synctest_draw_present'){
        assert.deepEqual(argTypes,[]);assert.deepEqual(args,[]);
      }else assert.fail(`Unexpected native call ${identifier}`);
      return 1;
    }};
  const mapping=[{sourceSlot:0,initializerPort:0,rawController:0},
    {sourceSlot:1,initializerPort:2,rawController:1}];
  const prior=Object.getOwnPropertyDescriptor(globalThis,'window');
  globalThis.window={__slippiSynctest:{module}};
  try{
    const result=await runBrowserSample({evaluate:(callback,args)=>new Function(`return (${callback.toString()})`)()(args)},307,mapping,[4,4],null,null,
      {pad44Base64:CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44,captureRawFiveLanes:true});
    assert.deepEqual(calls,['melee_web_snapshot_step_raw','melee_web_synctest_draw_present']);
    assert.equal(result.boundary.sourceFrame,177);assert.equal(result.pad,CPU_CORRECTED_TWO_WINDOW_NEUTRAL_PAD44);
    assert.equal(result.rawFiveLanes.pad,result.pad);
    assert.equal(decode(result.rawFiveLanes.observation).length,1280);
    assert.equal(decode(result.rawFiveLanes.pcm).length,4272);
    assert.ok(decode(result.rawFiveLanes.rngProfile).equals(Buffer.alloc(32,0x52)));
    assert.ok(decode(result.rawFiveLanes.transfer).equals(Buffer.alloc(40,0x63)));
    assert.equal(result.boundary.ready,1);assert.equal(result.boundary.interactive,true);
  }finally{
    if(prior)Object.defineProperty(globalThis,'window',prior);else delete globalThis.window;
  }
});
