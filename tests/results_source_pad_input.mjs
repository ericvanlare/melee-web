// Run as one page task so a browser timing pause cannot interleave between
// resuming Results and queuing the next source-tick PAD sample.
export function queueResultsP1StartAtCurrentSource({button,duration}){
  const status=document.querySelector('#status')?.textContent||'';
  const runningBeforeQueue=Module._melee_web_native_menu_running()!==0;
  let diagnostics=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
  const resumedAfterTimingPause=!runningBeforeQueue&&
    status.startsWith('Paused after a timing disruption');
  if(resumedAfterTimingPause){
    Module._melee_web_native_menu_pause(0);
    diagnostics=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
  }
  const frame=diagnostics.match(/(?:Results|Prize) source frame: (\d+)/);
  if(!frame)throw Error('Results source frame is unavailable at PAD queue boundary');
  const result=Module._melee_web_native_menu_pad_sample(0,button,0,0,duration);
  return {result,source_frame:Number(frame[1]),running_before_queue:runningBeforeQueue,
    resumed_after_timing_pause:resumedAfterTimingPause,
    status_before_queue:status,
    diagnostics:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())};
}

// Queue every raw Results input before SSS/match construction. The source loop
// consumes each event at its exact Results cursor; browser callback latency
// after this synchronous handoff cannot move or late-inject an event.
export function scheduleResultsP1StartSequence({events}){
  if(!Array.isArray(events)||events.length<1||events.length>8)
    throw Error('Invalid Results P1 source-tick schedule');
  let previous=-1;
  for(const event of events){
    if(!Number.isInteger(event?.targetFrame)||event.targetFrame<0||event.targetFrame>8191||
       !Number.isInteger(event?.button)||event.button!==0x1000||
       !Number.isInteger(event?.duration)||event.duration<1||event.duration>120||
       event.port!==0||event.targetFrame<=previous)
      throw Error('Invalid or unordered Results P1 source-tick event');
    previous=event.targetFrame;
  }
  const schedule=Module._melee_web_native_menu_results_pad_schedule;
  if(typeof schedule!=='function')throw Error('Exact Results source-tick scheduler is unavailable');
  const accepted=[];
  for(const event of events){
    const result=schedule(event.targetFrame,event.port,event.button,event.duration);
    if(result!==1){
      const diagnostics=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
      throw Error(`Results PAD schedule rejected at source tick ${event.targetFrame}: ${diagnostics}`);
    }
    accepted.push({...event,status:'scheduled-before-match'});
  }
  return {status:'scheduled',events:accepted};
}
