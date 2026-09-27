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
