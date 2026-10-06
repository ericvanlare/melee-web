/** Opt-in pause-trace capture for the CPU9 lineup and bounded timing harnesses.
 * Local diagnostic helper for the 2026-10-05 gameplay pause trace. It wraps the
 * development runtime's existing per-callback timing hook (window.menuRuntimeTiming),
 * the compact sample hook and the incident hook. Without a scheduled stall it
 * calls originals and stores published scalars in a preallocated Float64Array.
 * The optional fixed timing-matrix stall is an explicit host-side busy interval
 * inside the callback timing-observation hook, after source draws/submissions and
 * the original timing observer, before the native callback returns. */

export const PAUSE_TRACE_COLUMNS=Object.freeze([
  'hook_at_ms','started_ms','frame','total_ms','preparation_ms','input_ms','simulation_audio_ms',
  'begin_ms','draw_ms','end_ms','staging_slot_wait_ms','staging_slot_wait_count',
  'frame_slot_wait_ms','frame_slot_wait_count','begin_max_wait_ms','begin_record_ms',
  'end_gfx_finish_ms','end_queue_submit_ms','end_surface_encode_ms','end_observer_ms',
  'source_steps','source_draws','draw_calls','queued_delta','created_delta',
  'texture_upload_bytes','staging_used_bytes','wasm_heap_bytes','began','drawn','draw_suppressed',
  'sample_pending_ticks','sample_running','sample_source_frame','sample_scene',
  'sample_update_ms','sample_render_ms','sample_callback_ms']);

export function pauseTraceConfig(baseTrace){
  const trace=structuredClone(baseTrace);
  // Ring buffer: retain the most recent interval when the trace is ended at the
  // first timing pause. Wrapping is expected; coverage is checked offline.
  trace.traceConfig.recordMode='recordContinuously';
  trace.traceConfig.traceBufferSizeInKb=65536;
  return trace;
}

export async function installPauseTraceCapture(page,stallSchedule=null){
  if(stallSchedule!==null&&((stallSchedule.sourceFrame!==null&&(!Number.isSafeInteger(stallSchedule.sourceFrame)||stallSchedule.sourceFrame<0))||
    !Number.isSafeInteger(stallSchedule.replayCursor)||stallSchedule.replayCursor<0||
    !Number.isSafeInteger(stallSchedule.durationMs)||stallSchedule.durationMs<1||stallSchedule.durationMs>250))
    throw Error('Pause-trace stall schedule must name a optional nonnegative original match counter, exact nonnegative replay cursor and 1..250ms duration');
  return page.evaluate(({columns,stallSchedule})=>{
    if(window.__meleePauseTrace)return {status:'already-installed'};
    const COLS=columns.length,CAP=72000,RING=1800;
    const table=new Float64Array(CAP*COLS);
    const ring=new Array(RING);
    const state={columns,cap:CAP,rows:0,dropped:0,ring_size:RING,incidents:[],incident_overflow:0,
      sample:null,errors:0,installed_at_ms:performance.now(),time_origin:performance.timeOrigin,
      stall_schedule:stallSchedule?{...stallSchedule,status:'armed',actual:null,error:null}:null};
    const originalTiming=window.menuRuntimeTiming;
    const originalSample=window.menuDiagnosticSample;
    const originalIncident=window.menuDiagnosticIncident;
    window.menuDiagnosticSample=function(...args){
      try{return originalSample?.apply(this,args);}
      finally{state.sample=[args[8],args[18]?1:0,args[1],args[2],args[9],args[10],args[11]];}
    };
    window.menuDiagnosticIncident=function(...args){
      try{return originalIncident?.apply(this,args);}
      finally{
        if(state.incidents.length<256)state.incidents.push({at_ms:performance.now(),row:state.rows,
          reason:args[0],value:args[1],threshold:args[2],source_frame:args[3],scene:args[4],
          clock_owner:args[5]});
        else state.incident_overflow++;
      }
    };
    const maybeRunStall=sourceFrame=>{
      const schedule=state.stall_schedule;
      if(!schedule||schedule.status!=='armed'||!Number.isInteger(sourceFrame))return;
      const cursorOnly=schedule.sourceFrame===null;
      if(!cursorOnly&&sourceFrame<schedule.sourceFrame)return;
      const cursor=window.Module?._melee_web_native_menu_replay_cursor?.()??null;
      if(cursorOnly&&cursor<schedule.replayCursor)return;
      if((!cursorOnly&&sourceFrame!==schedule.sourceFrame)||cursor!==schedule.replayCursor){
        schedule.status='failed';
        schedule.error=!cursorOnly&&sourceFrame!==schedule.sourceFrame?'target_source_frame_skipped':'target_replay_cursor_mismatch';
        schedule.observed_source_frame=sourceFrame;
        schedule.observed_replay_cursor=cursor;
        return;
      }
      const startedAt=performance.now();
      schedule.status='running';
      schedule.started_at_ms=startedAt;
      schedule.started_epoch_ms=performance.timeOrigin+startedAt;
      schedule.observed_source_frame=sourceFrame;
      schedule.observed_replay_cursor=cursor;
      schedule.insertion_boundary='menuRuntimeTiming observer after original observer and source submission, before native callback return';
      const deadline=startedAt+schedule.durationMs;
      while(performance.now()<deadline){/* declared host-stall treatment */}
      const endedAt=performance.now();
      schedule.status='complete';
      schedule.ended_at_ms=endedAt;
      schedule.ended_epoch_ms=performance.timeOrigin+endedAt;
      schedule.actual_ms=endedAt-startedAt;
      schedule.replay_cursor_after=window.Module?._melee_web_native_menu_replay_cursor?.()??null;
    };
    window.menuRuntimeTiming=function(data){
      const hookAt=performance.now();
      try{return originalTiming?.apply(this,arguments);}
      finally{
        try{
          const s=state.sample||[];
          if(state.rows<CAP){
            const b=data?.begin_phases||{},e=data?.end_phases||{};
            const v=[hookAt,data?.started,data?.frame,data?.total_ms,data?.preparation_ms,data?.input_ms,
              data?.simulation_audio_ms,data?.begin_ms,data?.draw_ms,data?.end_ms,
              b.staging_slot_wait_ms,b.staging_slot_wait_count,b.frame_slot_wait_ms,b.frame_slot_wait_count,
              b.max_wait_ms,b.record_ms,e.gfx_finish_ms,e.queue_submit_ms,e.surface_encode_ms,e.observer_ms,
              data?.source_steps,data?.source_draws,data?.draw_calls,data?.queued_delta,data?.created_delta,
              data?.texture_upload_bytes,data?.staging_used_bytes,data?.wasm_heap_bytes,data?.began,
              data?.drawn,data?.draw_suppressed,s[0],s[1],s[2],s[3],s[4],s[5],s[6]];
            const base=state.rows*COLS;
            for(let index=0;index<COLS;index++){
              const value=Number(v[index]);table[base+index]=Number.isFinite(value)?value:NaN;
            }
            ring[state.rows%RING]=data;
            state.rows++;
          }else state.dropped++;
          maybeRunStall(s[2]);
          state.sample=null;
        }catch(_){state.errors++;}
      }
    };
    window.__meleePauseTrace={table,ring,state};
    return {status:'installed',columns:COLS,capacity:CAP,ring:RING,
      timing_hook_present:typeof originalTiming==='function',
      sample_hook_present:typeof originalSample==='function',
      incident_hook_present:typeof originalIncident==='function',
      stall_schedule_supported:!!state.stall_schedule};
  },{columns:PAUSE_TRACE_COLUMNS,stallSchedule});
}

export async function readPauseTraceStatus(page){
  return page.evaluate(()=>{
    const capture=window.__meleePauseTrace;
    if(!capture)return {status:'not-installed'};
    const module=window.Module;
    const call=name=>{try{return typeof module?.[name]==='function'?module[name]():null;}catch(error){return {error:String(error?.message||error)};}};
    const status=document.querySelector('#status');
    const dialog=document.querySelector('#error-dialog[open]');
    return {status:'installed',rows:capture.state.rows,dropped:capture.state.dropped,
      capture_errors:capture.state.errors,stall_schedule:capture.state.stall_schedule,
      incidents:capture.state.incidents.slice(),incident_overflow:capture.state.incident_overflow,
      replay_cursor:call('_melee_web_native_menu_replay_cursor'),source_running:call('_melee_web_native_menu_running'),
      phase:call('_melee_web_native_menu_phase'),runtime_error:status?.dataset.runtimeError||null,
      dialog_error:dialog?document.querySelector('#error')?.textContent?.trim()||'Application error':null,
      replay_report:window.lastRetailReplayReport??null,status_text:status?.textContent||null};
  });
}

/** Read the existing recorder's persisted incidents after the measured window.
 * The development owner is module-local; exportRetained is the existing public
 * recorder read API. This reader never triggers, persists or synthesizes an
 * incident. A native timing pause makes the original owner inactive and queues
 * its own persistence before this bounded read. */
export async function readRetainedPauseDiagnostics(page,waitForIncident=false){
  return page.evaluate(async wait=>{
    const {createRuntimeDiagnostics}=await import('./runtime-diagnostics.mjs');
    const reader=createRuntimeDiagnostics();
    const deadline=performance.now()+(wait?2000:0);
    let retained;
    do{
      retained=await reader.exportRetained();
      if(retained.records.length||performance.now()>=deadline)break;
      await new Promise(resolve=>setTimeout(resolve,50));
    }while(true);
    const nativeIncidents=window.__meleePauseTrace?.state.incidents??null;
    const observed=nativeIncidents?.length??null;
    // Match the existing producer: these are lifecycle events, not retained incidents.
    const qualifying=nativeIncidents?.filter(event=>![5,6,7,9].includes(event.reason)).length??null;
    return {read_api:'createRuntimeDiagnostics().exportRetained()',
      scope:'actual locally persisted runtime recorder incidents; reader metadata is not the producer session',
      status:retained.records.length?'retained_incidents':qualifying===0?'no_qualifying_incident_generated':'incident_not_retained',
      native_trace_incident_count:observed,
      qualifying_native_incident_count:qualifying,
      retained_records:retained.records,
      reader_flags:retained.flags};
  },waitForIncident);
}

export async function readPauseTraceCapture(page,reason){
  const captured=await page.evaluate(()=>{
    const capture=window.__meleePauseTrace;
    if(!capture)return {status:'not-installed'};
    const {table,ring,state}=capture;
    const cols=state.columns.length,rows=state.rows;
    const flat=Array.from(table.subarray(0,rows*cols),value=>Number.isFinite(value)?value:null);
    const ringRows=[];
    for(let index=Math.max(0,rows-state.ring_size);index<rows;index++)ringRows.push({row:index,data:ring[index%state.ring_size]});
    const audio=window.__meleeWebAudioDiagnostics?.snapshot?.()||null;
    const audioHistory=window.__meleePauseTraceAudioHistory?.slice?.()||null;
    const hitch=window.meleeHitchCapture?.report?.()||null;
    const marks=performance.getEntriesByType('mark').filter(mark=>mark.name.startsWith('pause-trace-'))
      .map(mark=>({name:mark.name,start_time:mark.startTime}));
    return {status:'captured',read_at_ms:performance.now(),time_origin:performance.timeOrigin,
      columns:state.columns,rows,dropped:state.dropped,errors:state.errors,cap:state.cap,
      installed_at_ms:state.installed_at_ms,incidents:state.incidents,
      stall_schedule:state.stall_schedule,
      incident_overflow:state.incident_overflow,table:flat,ring:ringRows,audio_snapshot:audio,
      audio_queue_history:audioHistory,hitch_capture:hitch,marks,
      status_text:document.querySelector('#status')?.textContent||null,
      audio_metrics_text:document.querySelector('#audio-metrics')?.textContent||null,
      render_metrics_text:document.querySelector('#render-metrics')?.textContent||null};
  });
  return {schema:'melee-web-pause-trace-capture-v1',reason,...captured};
}

/** Harness-side replacement for the development CSS observers. The served
 * observers allocate 16 bytes for ids while melee_web_css_observe_port writes
 * 14 int32 values (56 bytes). This copy uses the native declaration's size and
 * returns exactly the same four ids and eight geometry values. */
export async function installSizedCssObservers(page){
  return page.evaluate(()=>{
    const IDS_BYTES=14*4,GEOMETRY_BYTES=8*4;
    const read=(call)=>{const ids=Module._malloc(IDS_BYTES),geometry=Module._malloc(GEOMETRY_BYTES);
      if(!ids||!geometry){if(ids)Module._free(ids);if(geometry)Module._free(geometry);return null;}
      try{if(!call(ids,geometry))return null;
        return {ids:Array.from(Module.HEAP32.subarray(ids>>2,(ids>>2)+4)),
          geometry:Array.from(Module.HEAPF32.subarray(geometry>>2,(geometry>>2)+8))};}
      finally{Module._free(ids);Module._free(geometry);}};
    const replaced={port:typeof window.menuObserveFighterPort==='function',single:typeof window.menuObserveFighter==='function'};
    window.menuObserveFighterPort=(port,kind)=>{
      if(!Module._melee_web_native_menu_phase||Module._melee_web_native_menu_phase()!==1)return null;
      return read((ids,geometry)=>Module._melee_web_css_observe_port(port,kind,ids,geometry));};
    window.menuObserveFighter=kind=>read((ids,geometry)=>Module._melee_web_css_observe(kind,ids,geometry));
    return {status:'installed',ids_bytes:IDS_BYTES,geometry_bytes:GEOMETRY_BYTES,replaced};
  });
}
