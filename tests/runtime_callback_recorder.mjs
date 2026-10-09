/* Bounded callback recorder extracted from the existing CPU9 harness.
 * Values are observations only; original callbacks retain their return/throw behavior. */
export async function installRuntimeDiagnosticsCapture(identity,identityScope,targetPage){
  await targetPage.evaluate(({identity,identityScope})=>{
    const MAX_SAMPLES=100,MAX_INCIDENTS=24,SAMPLE_INTERVAL_MS=100;
    const capture={schema:'melee-web-runtime-callback-capture-v1',identity,identity_scope:identityScope,
      max_samples:MAX_SAMPLES,max_incidents:MAX_INCIDENTS,samples:[],incidents:[],
      dropped_samples:0,dropped_incidents:0,status:'installed',last_at_ms:null,last_sample_at_ms:null,
      reason_counts:Array(10).fill(0),unknown_reason_count:0,
      dropped_reason_counts:Array(10).fill(0),dropped_unknown_reason_count:0,
      invalid_preparation_count:0,dropped_invalid_preparation_count:0,
      callback_count:0,max_callback_ms:null,max_interval_ms:null,max_update_ms:null,max_draw_ms:null,
      max_total_ms:null,max_preparation_ms:null,
      phase_source_steps:Array(16).fill(0),invalid_phase_steps:0};
    const finite=value=>Number.isFinite(value)?value:null;
    const integer=value=>Number.isFinite(value)&&Number.isInteger(value)?value:null;
    const structuredPreparation=row=>row.reason_code===7&&row.value===0&&row.threshold===0&&
      row.clock_owner_code===0&&Number.isInteger(row.source_frame)&&row.source_frame>=-1&&
      Number.isInteger(row.scene)&&row.scene>=0;
    const maximum=(current,value)=>value===null?current:current===null?value:Math.max(current,value);
    const originalSample=globalThis.menuDiagnosticSample;
    const originalIncident=globalThis.menuDiagnosticIncident;
    globalThis.menuDiagnosticSample=(...args)=>{
      const started=performance.now();
      let result;
      try{result=originalSample?.(...args);}
      finally{
        const ended=performance.now();
        const callbackMs=finite(Math.max(0,ended-started));
        const intervalMs=capture.last_at_ms===null?null:finite(Math.max(0,ended-capture.last_at_ms));
        const updateMs=finite(args[9]),drawMs=finite(args[10]),totalMs=finite(args[11]),
          preparationMs=finite(args[12]);
        capture.callback_count++;
        // Count every callback before sample thinning. The tag is the phase
        // observed at callback publication, not a claim that all its steps
        // occurred within that phase when a transition happened mid-callback.
        const phase=integer(args[2]),steps=integer(args[16]);
        if(phase!==null&&phase>=0&&phase<16&&steps!==null&&steps>=0&&
           Number.isSafeInteger(capture.phase_source_steps[phase]+steps))
          capture.phase_source_steps[phase]+=steps;
        else capture.invalid_phase_steps++;
        capture.max_callback_ms=maximum(capture.max_callback_ms,callbackMs);
        capture.max_interval_ms=maximum(capture.max_interval_ms,intervalMs);
        capture.max_update_ms=maximum(capture.max_update_ms,updateMs);
        capture.max_draw_ms=maximum(capture.max_draw_ms,drawMs);
        capture.max_total_ms=maximum(capture.max_total_ms,totalMs);
        capture.max_preparation_ms=maximum(capture.max_preparation_ms,preparationMs);
        const retain=capture.last_sample_at_ms===null||ended-capture.last_sample_at_ms>=SAMPLE_INTERVAL_MS;
        capture.last_at_ms=ended;
        if(retain){
          capture.samples.push({at_ms:finite(ended),timestamp:finite(args[0]),source_frame:integer(args[1]),
            scene:integer(args[2]),stage:integer(args[3]),fighter0:finite(args[4]),fighter1:finite(args[5]),
            fighter2:finite(args[6]),fighter3:finite(args[7]),debt_ticks:finite(args[8]),update_ms:updateMs,
            draw_ms:drawMs,total_ms:totalMs,preparation_ms:preparationMs,queued_delta:finite(args[13]),
            created_delta:finite(args[14]),texture_upload_bytes:finite(args[15]),source_steps:finite(args[16]),
            source_draws:finite(args[17]),running:args[18]===undefined?null:!!args[18],callback_ms:callbackMs,
            interval_ms:intervalMs});
          if(capture.samples.length>MAX_SAMPLES){capture.samples.shift();capture.dropped_samples++;}
          capture.last_sample_at_ms=ended;
        }else capture.dropped_samples++;
      }
      return result;
    };
    globalThis.menuDiagnosticIncident=(...args)=>{
      const started=performance.now();
      let result;
      try{result=originalIncident?.(...args);}
      finally{
        const ended=performance.now();
        const reasonCode=integer(args[0]);
        const reasonBucket=reasonCode!==null&&reasonCode>=0&&reasonCode<10?reasonCode:null;
        if(reasonBucket===null)capture.unknown_reason_count++;else capture.reason_counts[reasonBucket]++;
        const row={at_ms:finite(ended),reason_code:reasonCode,value:finite(args[1]),
          threshold:finite(args[2]),source_frame:integer(args[3]),scene:integer(args[4]),
          clock_owner_code:integer(args[5]),callback_ms:finite(Math.max(0,ended-started))};
        if(reasonBucket===7&&!structuredPreparation(row))capture.invalid_preparation_count++;
        capture.incidents.push(row);
        if(capture.incidents.length>MAX_INCIDENTS){
          const dropped=capture.incidents.shift();capture.dropped_incidents++;
          const droppedReason=dropped.reason_code;
          const droppedBucket=droppedReason!==null&&droppedReason>=0&&droppedReason<10?droppedReason:null;
          if(droppedBucket===null)capture.dropped_unknown_reason_count++;
          else capture.dropped_reason_counts[droppedBucket]++;
          if(droppedBucket===7&&!structuredPreparation(dropped))capture.dropped_invalid_preparation_count++;
        }
      }
      return result;
    };
    globalThis.__meleeWebRuntimeIncidentCampaignCapture=capture;
  },{identity,identityScope});
}
