export function summarizeResultsPadTrace(trace){
  const startRuns=[];
  const pageTransitions=[];
  const previousPages=Array(4).fill(null);
  let previousSourceFrame=null;
  for(let index=0;index<trace.samples.length;index++){
    const sourceFrame=trace.samples[index].source_frame;
    if(!Number.isInteger(sourceFrame))
      throw Error(`Results source PAD trace row ${index} has no integer source frame`);
    if(previousSourceFrame!==null&&sourceFrame<=previousSourceFrame)
      throw Error(`Results source PAD trace crossed a session boundary at row ${index}: `+
        `${previousSourceFrame} -> ${sourceFrame}`);
    previousSourceFrame=sourceFrame;
  }
  for(const row of trace.samples){
    const state=row.results_state_after_tick;
    if(state){
      for(let slot=0;slot<4;slot++){
        const page=state.players[slot].page;
        if(previousPages[slot]!==null&&page!==previousPages[slot])
          pageTransitions.push({slot,from:previousPages[slot],to:page,
            source_frame:state.source_frame,phase:state.phase,
            stats_phase:state.stats_phase,confirmed:state.players[slot].confirmed});
        previousPages[slot]=page;
      }
    }
    if((row.pads[0].button&0x1000)===0)continue;
    const last=startRuns.at(-1);
    if(last&&row.source_frame===last.last_source_frame+1)last.last_source_frame=row.source_frame;
    else startRuns.push({first_source_frame:row.source_frame,last_source_frame:row.source_frame});
  }
  return {attempts:trace.attempts,retained:trace.retained,capacity:trace.capacity,
    overflow:trace.overflow,tick_returned:trace.samples.filter(row=>row.tick_returned).length,
    tick_failed:trace.samples.filter(row=>!row.tick_returned).map(row=>row.source_frame),
    p1_button_values:[...new Set(trace.samples.map(row=>row.pads[0].button))],
    p1_start_runs:startRuns,
    results_page_transitions:pageTransitions,
    port_error_values:Array.from({length:4},(_,port)=>
    [...new Set(trace.samples.map(row=>row.pads[port].err))])};
}

export function findResultsStartRunAtOrAfter(startRuns,sourceFrame){
  if(!Array.isArray(startRuns)||!Number.isInteger(sourceFrame))
    throw Error('Results confirmation lookup requires source PAD runs and an integer source frame');
  return startRuns.find(run=>Number.isInteger(run?.first_source_frame)&&
    run.first_source_frame>=sourceFrame)??null;
}

// Correlate every trusted keyboard pulse after the CPU page gate to the raw
// source PAD trace. A dispatched pulse can be ignored by the source; only an
// actual P1 Start sample inside that pulse's observed down/up source-frame
// bracket counts as confirmation.
export function findConsumedResultsStartKeyboardAttempt(keydowns,keyups,startRuns,
  afterSourceFrame){
  if(!Array.isArray(keydowns)||!Array.isArray(keyups)||!Array.isArray(startRuns)||
    keydowns.length!==keyups.length)
    throw Error('Results keyboard confirmation requires paired keydown/keyup events');
  const attempts=keydowns.map((keydown,index)=>{
    const keydownFrame=keydown?.resultsSourceFrameAtEvent;
    const keyupFrame=keyups[index]?.resultsSourceFrameAtEvent;
    const startRun=Number.isInteger(keydownFrame)&&Number.isInteger(keyupFrame)&&
      keyupFrame>=keydownFrame
      ?startRuns.find(run=>run.first_source_frame>afterSourceFrame&&
        keydownFrame<=run.first_source_frame&&keyupFrame>=run.first_source_frame)
      :undefined;
    return {index,keydown_source_frame:keydownFrame??null,
      keyup_source_frame:keyupFrame??null,
      consumed_start_source_frame:startRun?.first_source_frame??null};
  });
  return {attempts,accepted:attempts.find(attempt=>
    attempt.consumed_start_source_frame!==null)??null};
}

// The first valid P1 Start moves Results from its source fade into statistics.
// Return null until that initial one-button pulse is observed and both
// disconnected CPU pages have advanced. A later pulse is valid only after
// both source-observed page transitions, even when ordinary keyboard timing
// means it was already consumed before this observer was polled.
export function assertResultsCpuPagesAfterInitialP1Keyboard(trace,targetFrame){
  if(trace.overflow)throw Error('Results CPU-page gate received an overflowed source trace');
  const summary=summarizeResultsPadTrace(trace);
  const latest=trace.samples.at(-1)?.results_state_after_tick;
  if(!latest||latest.source_frame<targetFrame)return null;
  if(summary.tick_failed.length)
    throw Error('Results source tick failed before the keyboard confirmation boundary');
  if(summary.results_page_transitions.some(row=>
    row.from!==0||row.to!==1||row.slot<2||row.slot>3))
    throw Error('Connected neutral ports or an unexpected Results page changed before keyboard confirmation');
  const analogFields=['stick_x','stick_y','substick_x','substick_y','trigger_left',
    'trigger_right','analog_a','analog_b','ext_button'];
  if(trace.samples.some(row=>row.pads.some((pad,port)=>(
    port===0?pad.button!==0&&pad.button!==0x1000:pad.button!==0)||
    analogFields.some(field=>pad[field]!==undefined&&pad[field]!==0))))
    throw Error('A Results input other than P1 Start or neutral analog controls was consumed');
  if(JSON.stringify(summary.port_error_values)!==JSON.stringify([[0],[0],[-1],[-1]]))
    throw Error('Natural P1/P2-connected and CPU-P3/P4-disconnected controller status changed');
  const startRuns=summary.p1_start_runs;
  if(!startRuns.length)return null;
  const cpuTransitions=summary.results_page_transitions;
  const firstCpuTransition=cpuTransitions[0];
  const firstStart=startRuns[0];
  const beforeInitialStart=trace.samples.find(row=>
    row.source_frame===firstStart.first_source_frame-1);
  if(beforeInitialStart?.results_state_after_tick?.phase!==2)
    throw Error('The first P1 Start edge must be consumed from original Results phase 2, not ignored during its fade');
  if(!firstCpuTransition){
    if(startRuns.length>1)
      throw Error('An additional P1 Start was consumed before either disconnected CPU page auto-advanced');
    return null;
  }
  const firstPageFrame=Math.min(...cpuTransitions.map(row=>row.source_frame));
  const lastPageFrame=Math.max(...cpuTransitions.map(row=>row.source_frame));
  const initialStart=startRuns.find(run=>run.last_source_frame<firstPageFrame);
  if(!initialStart)
    throw Error('No initial P1 Start completed before the first disconnected CPU page auto-advanced');
  const pageOverlapStarts=startRuns.filter(run=>run!==initialStart&&
    run.first_source_frame<=lastPageFrame);
  if(pageOverlapStarts.length)
    throw Error('An additional P1 Start was consumed before both disconnected CPU pages auto-advanced');
  const statsPhaseStart=trace.samples.find(row=>{
    const state=row.results_state_after_tick;
    return state?.phase===3&&state.stats_phase===2&&
      state.players[2].page===0&&state.players[3].page===0;
  })?.results_state_after_tick?.source_frame;
  if(!Number.isInteger(statsPhaseStart)||statsPhaseStart<initialStart.first_source_frame)return null;
  if(JSON.stringify(cpuTransitions.map(row=>row.slot))!==JSON.stringify([2,3]))return null;
  if(!cpuTransitions.every(row=>row.phase===3&&row.stats_phase===2&&
       row.source_frame>statsPhaseStart&&
       row.source_frame>initialStart.last_source_frame&&
       row.source_frame<=latest.source_frame)||
      ![3,4].includes(latest.phase)||latest.stats_phase!==2)
    return null;
  const postPageStartRuns=startRuns.filter(run=>run.first_source_frame>lastPageFrame);
  return {source_frame:latest.source_frame,initial_start:initialStart,
    post_page_start_runs:postPageStartRuns,
    stats_phase_start_source_frame:statsPhaseStart,
    cpu_page_delay_source_ticks:cpuTransitions.map(row=>({slot:row.slot,
      ticks:row.source_frame-statsPhaseStart})),transitions:cpuTransitions,summary};
}

export function buildResultsPadTraceRecord(match,reason,trace){
  return {match,reason,summary:summarizeResultsPadTrace(trace),trace};
}
