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

// Observe the live source cursor from the page's frame queue and dispatch only
// while that exact Results sample boundary is current. Unlike a host-side
// wait-then-evaluate sequence, this never sends a late PAD pulse after a
// source-tick target was missed.
export function queueResultsP1StartAtExactSourceTick({targetFrame,button,duration}){
  if(!Number.isInteger(targetFrame)||targetFrame<0||
     !Number.isInteger(button)||button<0||button>0xffff||
     !Number.isInteger(duration)||duration<1||duration>120)
    throw Error('Invalid exact Results source-tick PAD request');
  return new Promise(resolve=>{
    let finished=false;
    const finish=result=>{
      if(finished)return;
      finished=true;
      clearTimeout(timeout);
      resolve(result);
    };
    const timeout=setTimeout(()=>finish({status:'source-tick-timeout',target_source_frame:targetFrame}),60000);
    const poll=()=>{
      if(finished)return;
      try{
        const phase=Module._melee_web_native_menu_phase();
        if(phase===1){finish({status:'natural-css',target_source_frame:targetFrame});return;}
        if(phase!==8&&phase!==9){
          finish({status:'unexpected-phase',phase,target_source_frame:targetFrame});return;
        }
        const status=document.querySelector('#status')?.textContent||'';
        const runningBeforeQueue=Module._melee_web_native_menu_running()!==0;
        let diagnostics=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
        const readFrame=()=>{
          const frame=diagnostics.match(/(?:Results|Prize) source frame: (\d+)/);
          if(!frame)throw Error('Results source frame is unavailable at exact PAD boundary');
          return Number(frame[1]);
        };
        let sourceFrame=readFrame();
        if(sourceFrame>targetFrame){
          finish({status:'missed-source-tick',target_source_frame:targetFrame,
            source_frame:sourceFrame,status_before_queue:status});return;
        }
        if(sourceFrame<targetFrame){requestAnimationFrame(poll);return;}
        const resumedAfterTimingPause=!runningBeforeQueue&&
          status.startsWith('Paused after a timing disruption');
        if(!runningBeforeQueue){
          if(!resumedAfterTimingPause){
            finish({status:'not-running-at-source-tick',target_source_frame:targetFrame,
              source_frame:sourceFrame,status_before_queue:status});return;
          }
          Module._melee_web_native_menu_pause(0);
          diagnostics=Module.UTF8ToString(Module._melee_web_native_menu_diagnostics());
          sourceFrame=readFrame();
          if(sourceFrame!==targetFrame){
            finish({status:'missed-source-tick-after-resume',target_source_frame:targetFrame,
              source_frame:sourceFrame,status_before_queue:status});return;
          }
        }
        const result=Module._melee_web_native_menu_pad_sample(0,button,0,0,duration);
        finish({status:result===1?'queued':'queue-failed',result,target_source_frame:targetFrame,
          source_frame:sourceFrame,running_before_queue:runningBeforeQueue,
          resumed_after_timing_pause:resumedAfterTimingPause,status_before_queue:status,
          diagnostics:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics())});
      }catch(error){finish({status:'observer-error',target_source_frame:targetFrame,
        error:error?.message||String(error)});}
    };
    requestAnimationFrame(poll);
  });
}
