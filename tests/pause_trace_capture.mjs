/** Opt-in, observation-only pause-trace capture for the CPU9 lineup harness.
 * Local diagnostic helper for the 2026-10-05 gameplay pause trace. It wraps the
 * development runtime's existing per-callback timing hook (window.menuRuntimeTiming),
 * the compact sample hook and the incident hook. Each wrapper calls the original
 * first and stores already-published scalars in a preallocated Float64Array; it
 * never changes source scheduling, clocks, input, audio or GPU work. */

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

export async function installPauseTraceCapture(page){
  return page.evaluate(columns=>{
    if(window.__meleePauseTrace)return {status:'already-installed'};
    const COLS=columns.length,CAP=72000,RING=1800;
    const table=new Float64Array(CAP*COLS);
    const ring=new Array(RING);
    const state={columns,cap:CAP,rows:0,dropped:0,ring_size:RING,incidents:[],incident_overflow:0,
      sample:null,errors:0,installed_at_ms:performance.now(),time_origin:performance.timeOrigin};
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
    window.menuRuntimeTiming=function(data){
      const hookAt=performance.now();
      try{return originalTiming?.apply(this,arguments);}
      finally{
        try{
          if(state.rows<CAP){
            const b=data?.begin_phases||{},e=data?.end_phases||{},s=state.sample||[];
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
          state.sample=null;
        }catch(_){state.errors++;}
      }
    };
    window.__meleePauseTrace={table,ring,state};
    return {status:'installed',columns:COLS,capacity:CAP,ring:RING,
      timing_hook_present:typeof originalTiming==='function',
      sample_hook_present:typeof originalSample==='function',
      incident_hook_present:typeof originalIncident==='function'};
  },PAUSE_TRACE_COLUMNS);
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
