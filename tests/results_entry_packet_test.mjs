import assert from 'node:assert/strict';
import {bindResultsEntryPacket,readResultsEntryPacket} from './results_entry_packet.mjs';
import {queueResultsP1StartAtCurrentSource,queueResultsP1StartAtExactSourceTick}
  from './results_source_pad_input.mjs';
import {assertResultsCpuPagesAfterInitialP1Keyboard,buildResultsPadTraceRecord,
  findConsumedResultsStartKeyboardAttempt,findResultsStartRunAtOrAfter,
  summarizeResultsPadTrace}
  from './results_source_pad_trace.mjs';

const packet={schema:'melee-web-results-entry-v1',abi:{target:'wasm32',byte_order:'little-endian',pointer_bytes:4},
  match_index:1,entry_seed:0xfedcba98,
  sizeof:{MatchExitInfo:2,ResultsMatchInfo:3},terminal_hex:'aa00',results_info_hex:'bb0000',pad:{bytes:822,hex:'00'.repeat(822)}};
const artifacts=['js','wasm'].map(extension=>({url:`http://127.0.0.1:8871/gameplay_menu_browser.${extension}?v=1`,
  bytes:100,status:200,sha256:'a'.repeat(64)}));
artifacts[1].hash_basis={method:'same-origin-static-file-sha256-plus-http-metadata',
  path:'build/browser/gameplay_menu_browser.wasm',content_length:100,content_encoding:null};
const bound=bindResultsEntryPacket(packet,artifacts);
assert.equal(bound.build_binding.status,'bound');
assert.equal(bound.build_binding.kind,'http-response-metadata-plus-static-file-sha256');
assert.deepEqual(bound.build_binding.artifacts[1].hash_basis,artifacts[1].hash_basis);
assert.deepEqual(bound.packet,packet);
assert.throws(()=>bindResultsEntryPacket(packet,[]),/Missing served/);
assert.throws(()=>bindResultsEntryPacket(packet,[...artifacts,{...artifacts[1],sha256:'b'.repeat(64)}]),/Conflicting/);
assert.throws(()=>bindResultsEntryPacket({...packet,results_info_hex:'bb'},artifacts));
assert.throws(()=>bindResultsEntryPacket({...packet,abi:{...packet.abi,byte_order:'big-endian'}},artifacts));
let calls=0;
const page={evaluate:fn=>fn()};
assert.deepEqual(await readResultsEntryPacket(page),{status:'getter-unavailable'});
globalThis.Module={UTF8ToString:text=>text,_melee_web_native_menu_results_entry_packet:()=>{calls++;return 'null';}};
assert.equal((await readResultsEntryPacket(page)).status,'no-entry');
Module._melee_web_native_menu_results_entry_packet=()=>{calls++;return JSON.stringify(packet);};
assert.deepEqual((await readResultsEntryPacket(page)).packet,packet);
assert.equal(calls,2);
delete globalThis.Module;

const resultsPadTrace={attempts:5,retained:5,capacity:8192,overflow:false,
  camera_entry:{saved_pool_at_context_begin:'0x0',source_pool_before_onenter:'0x0',
    owner_pool_before_onenter:'0x0',source_pool_after_onenter:'0x1234',
    source_pool_after_collision_adoption:'0x1234',context_pool_after_adoption:'0x1234',
    owner_pool_after_adoption:'0x1234'},
  samples:[
    {source_frame:408,tick_returned:true,pads:[0,0,0,0].map((_,port)=>({button:0,err:port<2?0:-1})),
      results_state_after_tick:{source_frame:409,phase:3,stats_phase:2,
        players:[0,1,2,3].map(()=>({page:0,confirmed:0}))}},
    {source_frame:409,tick_returned:true,pads:[0,0,0,0].map((_,port)=>({button:0,err:port<2?0:-1})),
      results_state_after_tick:{source_frame:410,phase:3,stats_phase:2,
        players:[{page:0,confirmed:0},{page:0,confirmed:0},
          {page:1,confirmed:0},{page:1,confirmed:0}]}},
    ...[500,501,502].map(source_frame=>({source_frame,tick_returned:true,
      pads:[0,0,0,0].map((_,port)=>({button:port===0&&source_frame<502?4096:0,err:port<2?0:-1})),
      results_state_after_tick:null})),
  ]};
const retainedPadTrace=buildResultsPadTraceRecord(1,'natural-results-to-css',resultsPadTrace);
assert.strictEqual(retainedPadTrace.trace,resultsPadTrace);
assert.deepEqual(retainedPadTrace.trace.camera_entry,resultsPadTrace.camera_entry);
assert.equal(retainedPadTrace.summary.tick_returned,5);
assert.deepEqual(retainedPadTrace.summary.results_page_transitions.map(row=>row.slot),[2,3]);
assert.deepEqual(retainedPadTrace.summary.p1_start_runs,
  [{first_source_frame:500,last_source_frame:501}]);
assert.deepEqual(retainedPadTrace.summary.port_error_values,[[0],[0],[-1],[-1]]);
const sourceTickStartRuns=[{first_source_frame:206,last_source_frame:215},
  {first_source_frame:610,last_source_frame:619}];
assert.deepEqual(findResultsStartRunAtOrAfter(sourceTickStartRuns,600),
  sourceTickStartRuns[1],
  'Source-tick validation must bind confirmation to the actually consumed post-target PAD run');
assert.equal(findResultsStartRunAtOrAfter(sourceTickStartRuns,611),null,
  'A queued confirmation with no consumed source PAD run must not pass');
assert.throws(()=>findResultsStartRunAtOrAfter(sourceTickStartRuns,NaN),
  /integer source frame/);
const reusedObserverTrace={...resultsPadTrace,attempts:7,retained:7,samples:[
  ...resultsPadTrace.samples.slice(0,2),
  {...resultsPadTrace.samples[0],source_frame:1,
    results_state_after_tick:{...resultsPadTrace.samples[0].results_state_after_tick,
      source_frame:2,phase:0,stats_phase:0}},
]};
assert.throws(()=>summarizeResultsPadTrace(reusedObserverTrace),/crossed a session boundary/,
  'A retained trace spanning two Results contexts must not be treated as one session');
const neutralPads=()=>[0,1,2,3].map(port=>({button:0,err:port<2?0:-1}));
const firstStatsPageState=source_frame=>({source_frame,phase:3,stats_phase:2,
  players:[0,1,2,3].map(()=>({page:0,confirmed:0}))});
const preKeyboardTrace={attempts:4,retained:4,capacity:8192,overflow:false,samples:[
  ...resultsPadTrace.samples.slice(0,2),
  {source_frame:498,tick_returned:true,pads:neutralPads(),results_state_after_tick:firstStatsPageState(499)},
  {source_frame:499,tick_returned:true,pads:neutralPads(),results_state_after_tick:firstStatsPageState(500)},
]};
const preStatisticsTrace={...preKeyboardTrace,samples:[
  {source_frame:515,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{source_frame:516,phase:2,stats_phase:0,
      players:[0,1,2,3].map(()=>({page:0,confirmed:0}))}},
]};
assert.equal(assertResultsCpuPagesAfterInitialP1Keyboard(preStatisticsTrace,509),null,
  'The source-frame lower bound must not be mistaken for a consumed initial Enter or active statistics');
const retryKeydowns=[520,615,720,827,931].map(resultsSourceFrameAtEvent=>({
  resultsSourceFrameAtEvent}));
const retryKeyups=[524,616,721,828,938].map(resultsSourceFrameAtEvent=>({
  resultsSourceFrameAtEvent}));
const lateConsumedConfirmation=findConsumedResultsStartKeyboardAttempt(retryKeydowns,
  retryKeyups,[{first_source_frame:932,last_source_frame:938}],493);
assert.equal(lateConsumedConfirmation.accepted?.index,4,
  'A later trusted Enter pulse may be the first one actually consumed after page auto-advance');
assert.equal(lateConsumedConfirmation.accepted?.consumed_start_source_frame,932);
assert.deepEqual(lateConsumedConfirmation.attempts.slice(0,4).map(row=>
  row.consumed_start_source_frame),[null,null,null,null],
  'Dispatched but unconsumed Enter attempts must remain distinguishable from source PAD');
assert.equal(findConsumedResultsStartKeyboardAttempt(retryKeydowns,retryKeyups,
  [{first_source_frame:493,last_source_frame:500}],493).accepted,null,
  'A Start at the final CPU page transition is not a post-page confirmation');
assert.equal(findConsumedResultsStartKeyboardAttempt(retryKeydowns,retryKeyups,
  [{first_source_frame:932,last_source_frame:938}],950).accepted,null,
  'No source-consumed Start after the required page boundary must still fail');
const keyboardGateTrace={attempts:5,retained:5,capacity:8192,overflow:false,samples:[
  {source_frame:508,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{source_frame:509,phase:2,stats_phase:0,
      players:[0,1,2,3].map(()=>({page:0,confirmed:0}))}},
  {source_frame:509,tick_returned:true,pads:[{button:4096,err:0},...neutralPads().slice(1)],
    results_state_after_tick:{source_frame:510,phase:3,stats_phase:2,
      players:[0,1,2,3].map(()=>({page:0,confirmed:0}))}},
  {source_frame:510,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:firstStatsPageState(511)},
  {source_frame:689,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(690),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}},
  {source_frame:690,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(691),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}},
]};
const pageGate=assertResultsCpuPagesAfterInitialP1Keyboard(keyboardGateTrace,509);
assert.equal(pageGate.source_frame,691);
assert.deepEqual(pageGate.transitions.map(row=>row.slot),[2,3]);
assert.deepEqual(pageGate.initial_start,{first_source_frame:509,last_source_frame:509});
assert.deepEqual(pageGate.post_page_start_runs,[]);
assert.equal(pageGate.stats_phase_start_source_frame,510);
assert.deepEqual(pageGate.cpu_page_delay_source_ticks,[{slot:2,ticks:180},{slot:3,ticks:180}]);
const fadeStartTrace={...keyboardGateTrace,samples:keyboardGateTrace.samples.map((row,index)=>
  index===0?{...row,results_state_after_tick:{...row.results_state_after_tick,phase:1}}:row)};
assert.throws(()=>assertResultsCpuPagesAfterInitialP1Keyboard(fadeStartTrace,509),
  /first P1 Start edge must be consumed from original Results phase 2/,
  'A Start consumed during phase 1 must not count as the statistics trigger');
const nonNeutralResultsInput={...keyboardGateTrace,samples:keyboardGateTrace.samples.map((row,index)=>
  index===0?{...row,pads:[{...row.pads[0],stick_x:1},...row.pads.slice(1)]}:row)};
assert.throws(()=>assertResultsCpuPagesAfterInitialP1Keyboard(nonNeutralResultsInput,509),
  /neutral analog controls/,
  'The keyboard-only discriminator must reject an unrelated analog input');
const postTransitionKeyboardTrace={attempts:6,retained:6,capacity:8192,overflow:false,samples:[
  {source_frame:313,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(314),phase:2}},
  {source_frame:314,tick_returned:true,
    pads:[{button:4096,err:0},...neutralPads().slice(1)],
    results_state_after_tick:firstStatsPageState(315)},
  {source_frame:315,tick_returned:true,
    pads:[{button:4096,err:0},...neutralPads().slice(1)],
    results_state_after_tick:firstStatsPageState(316)},
  {source_frame:493,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:firstStatsPageState(494)},
  {source_frame:494,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(495),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}},
  {source_frame:523,tick_returned:true,
    pads:[{button:4096,err:0},...neutralPads().slice(1)],
    results_state_after_tick:{...firstStatsPageState(524),phase:4,players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}},
  {source_frame:536,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(537),phase:4,players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}}
]};
const alreadyConfirmedAfterPages=assertResultsCpuPagesAfterInitialP1Keyboard(
  postTransitionKeyboardTrace,509);
assert.deepEqual(alreadyConfirmedAfterPages.initial_start,
  {first_source_frame:314,last_source_frame:315});
assert.equal(alreadyConfirmedAfterPages.stats_phase_start_source_frame,315,
  'The statistics phase may begin during the held initial keyboard Start');
assert.deepEqual(alreadyConfirmedAfterPages.cpu_page_delay_source_ticks,
  [{slot:2,ticks:180},{slot:3,ticks:180}]);
assert.deepEqual(alreadyConfirmedAfterPages.post_page_start_runs,
  [{first_source_frame:523,last_source_frame:523}],
  'A Start already consumed strictly after both CPU transitions is a valid confirmation');
const oneCpuPageTrace={...preKeyboardTrace,samples:[
  {source_frame:699,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:firstStatsPageState(700)},
  {source_frame:700,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(701),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:0,confirmed:0}]}},
  {source_frame:701,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(702),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:0,confirmed:0}]}},
]};
assert.equal(assertResultsCpuPagesAfterInitialP1Keyboard(oneCpuPageTrace,509),null,
  'The source-tick gate must wait for both disconnected CPU page transitions');
const connectedPageTrace={...preKeyboardTrace,samples:[
  {source_frame:699,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:firstStatsPageState(700)},
  {source_frame:700,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(701),players:[{page:1,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}},
]};
assert.throws(()=>assertResultsCpuPagesAfterInitialP1Keyboard(connectedPageTrace,509),
  /unexpected Results page changed/);
const postPageKeyboardTrace={...keyboardGateTrace,samples:[...keyboardGateTrace.samples,
  {...keyboardGateTrace.samples.at(-1),source_frame:692,
    results_state_after_tick:{...keyboardGateTrace.samples.at(-1).results_state_after_tick,
      source_frame:693,phase:4},pads:[{button:4096,err:0},...neutralPads().slice(1)]}]};
assert.deepEqual(assertResultsCpuPagesAfterInitialP1Keyboard(postPageKeyboardTrace,509)
  .post_page_start_runs,[{first_source_frame:692,last_source_frame:692}],
  'A distinct ordinary P1 Start after both auto transitions is a valid confirmation');
const prePageKeyboardTrace={...keyboardGateTrace,samples:[
  ...keyboardGateTrace.samples.slice(0,3),
  {source_frame:520,tick_returned:true,
    pads:[{button:4096,err:0},...neutralPads().slice(1)],
    results_state_after_tick:firstStatsPageState(521)},
  ...keyboardGateTrace.samples.slice(3)
]};
assert.throws(()=>assertResultsCpuPagesAfterInitialP1Keyboard(prePageKeyboardTrace,509),
  /additional P1 Start/,
  'A distinct P1 Start before either CPU page transition is not confirmation');
const confirmationDuringStaggeredPages={...keyboardGateTrace,samples:[
  ...keyboardGateTrace.samples.slice(0,3),
  {source_frame:519,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(520),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:0,confirmed:0}]}},
  {source_frame:521,tick_returned:true,
    pads:[{button:4096,err:0},...neutralPads().slice(1)],
    results_state_after_tick:{...firstStatsPageState(522),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:0,confirmed:0}]}},
  {source_frame:529,tick_returned:true,pads:neutralPads(),
    results_state_after_tick:{...firstStatsPageState(530),players:[{page:0,confirmed:0},
      {page:0,confirmed:0},{page:1,confirmed:0},{page:1,confirmed:0}]}}
]};
assert.throws(()=>assertResultsCpuPagesAfterInitialP1Keyboard(confirmationDuringStaggeredPages,509),
  /additional P1 Start/,
  'A confirmation between staggered CPU-page transitions must remain rejected');

const pauseEvents=[];
const diagnostics=[
  'Original Results · Results source frame: 600',
  'Original Results · Results source frame: 600',
];
globalThis.document={querySelector:selector=>selector==='#status'?
  {textContent:'Paused after a timing disruption. Resume to continue.'}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_running:()=>{pauseEvents.push('running');return 0;},
  _melee_web_native_menu_diagnostics:()=>{
    pauseEvents.push('diagnostics');
    return diagnostics.shift()||'Original Results · Results source frame: 600';
  },
  _melee_web_native_menu_pause:value=>pauseEvents.push(`pause:${value}`),
  _melee_web_native_menu_pad_sample:(port,button,_x,_y,duration)=>{
    pauseEvents.push(`pad:${port}:${button}:${duration}`);
    return 1;
  },
};
const afterTimingPause=queueResultsP1StartAtCurrentSource({button:4096,duration:10});
assert.deepEqual(afterTimingPause,{result:1,source_frame:600,resumed_after_timing_pause:true,
  running_before_queue:false,
  status_before_queue:'Paused after a timing disruption. Resume to continue.',
  diagnostics:'Original Results · Results source frame: 600'});
assert.deepEqual(pauseEvents,['running','diagnostics','pause:0','diagnostics','pad:0:4096:10','diagnostics']);

const normalEvents=[];
globalThis.document={querySelector:selector=>selector==='#status'?
  {textContent:'Original Results'}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_running:()=>{normalEvents.push('running');return 1;},
  _melee_web_native_menu_diagnostics:()=>{
    normalEvents.push('diagnostics');
    return 'Original Results · Results source frame: 601';
  },
  _melee_web_native_menu_pause:value=>normalEvents.push(`pause:${value}`),
  _melee_web_native_menu_pad_sample:(port,button,_x,_y,duration)=>{
    normalEvents.push(`pad:${port}:${button}:${duration}`);
    return 1;
  },
};
const withoutPause=queueResultsP1StartAtCurrentSource({button:4096,duration:10});
assert.equal(withoutPause.resumed_after_timing_pause,false);
assert.deepEqual(normalEvents,['running','diagnostics','pad:0:4096:10','diagnostics']);

const unsupportedPauseEvents=[];
globalThis.document={querySelector:selector=>selector==='#status'?
  {textContent:'Paused.'}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_running:()=>{unsupportedPauseEvents.push('running');return 0;},
  _melee_web_native_menu_diagnostics:()=>{
    unsupportedPauseEvents.push('diagnostics');
    return 'Original Results · Results source frame: 602';
  },
  _melee_web_native_menu_pause:value=>unsupportedPauseEvents.push(`pause:${value}`),
  _melee_web_native_menu_pad_sample:()=>{
    unsupportedPauseEvents.push('pad');
    return 0;
  },
};
const unsupportedPause=queueResultsP1StartAtCurrentSource({button:4096,duration:10});
assert.equal(unsupportedPause.result,0);
assert.equal(unsupportedPause.resumed_after_timing_pause,false);
assert.deepEqual(unsupportedPauseEvents,['running','diagnostics','pad','diagnostics']);
delete globalThis.Module;
delete globalThis.document;

const savedRaf=globalThis.requestAnimationFrame;
const savedModule=globalThis.Module;
const savedDocument=globalThis.document;
let rafCallbacks=[];
globalThis.requestAnimationFrame=callback=>{rafCallbacks.push(callback);return rafCallbacks.length;};
const runRaf=()=>{
  assert(rafCallbacks.length,'Expected the exact source-tick watcher to await an animation frame');
  const callbacks=rafCallbacks.splice(0);
  for(const callback of callbacks)callback(0);
};
let livePhase=8,liveFrame=179,liveRunning=true,liveStatus='Original Results';
const exactEvents=[];
globalThis.document={querySelector:selector=>selector==='#status'?{textContent:liveStatus}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_phase:()=>livePhase,
  _melee_web_native_menu_running:()=>liveRunning?1:0,
  _melee_web_native_menu_diagnostics:()=>`Original Results · Results source frame: ${liveFrame}`,
  _melee_web_native_menu_pause:value=>{exactEvents.push(`pause:${value}`);liveRunning=value===0;},
  _melee_web_native_menu_pad_sample:(port,button,_x,_y,duration)=>{
    exactEvents.push(`pad:${port}:${button}:${duration}`);return 1;
  },
};
const waitingForExactTick=queueResultsP1StartAtExactSourceTick(
  {targetFrame:180,button:4096,duration:10});
runRaf();
assert.deepEqual(exactEvents,[],
  'The page-side watcher must not queue early while the source cursor precedes its target');
assert.equal(rafCallbacks.length,1,'The exact watcher must continue waiting in the page RAF queue');
liveFrame=180;
runRaf();
assert.deepEqual(await waitingForExactTick,{status:'queued',result:1,target_source_frame:180,
  source_frame:180,running_before_queue:true,resumed_after_timing_pause:false,
  status_before_queue:'Original Results',diagnostics:'Original Results · Results source frame: 180'});
assert.deepEqual(exactEvents,['pad:0:4096:10']);

liveFrame=211;
const missedExactTick=queueResultsP1StartAtExactSourceTick(
  {targetFrame:210,button:4096,duration:10});
runRaf();
assert.deepEqual(await missedExactTick,{status:'missed-source-tick',target_source_frame:210,
  source_frame:211,status_before_queue:'Original Results'});
assert.deepEqual(exactEvents,['pad:0:4096:10'],
  'A missed source tick must never degrade into a late PAD injection');

livePhase=1;
const naturalCss=queueResultsP1StartAtExactSourceTick(
  {targetFrame:240,button:4096,duration:10});
runRaf();
assert.deepEqual(await naturalCss,{status:'natural-css',target_source_frame:240});
assert.deepEqual(exactEvents,['pad:0:4096:10']);

livePhase=8;liveFrame=600;liveRunning=false;
liveStatus='Paused after a timing disruption. Resume to continue.';
const resumedAtExactTick=queueResultsP1StartAtExactSourceTick(
  {targetFrame:600,button:4096,duration:10});
runRaf();
assert.deepEqual(await resumedAtExactTick,{status:'queued',result:1,target_source_frame:600,
  source_frame:600,running_before_queue:false,resumed_after_timing_pause:true,
  status_before_queue:'Paused after a timing disruption. Resume to continue.',
  diagnostics:'Original Results · Results source frame: 600'});
assert.deepEqual(exactEvents,['pad:0:4096:10','pause:0','pad:0:4096:10']);
if(savedRaf===undefined)delete globalThis.requestAnimationFrame;
else globalThis.requestAnimationFrame=savedRaf;
if(savedModule===undefined)delete globalThis.Module;else globalThis.Module=savedModule;
if(savedDocument===undefined)delete globalThis.document;else globalThis.document=savedDocument;
console.log('Results entry binding and exact source-tick PAD queue boundary pass');
