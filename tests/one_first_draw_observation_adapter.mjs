import {readColorConversionDependencies} from './color_conversion_facts.mjs';
// Reuse the bounded normal-callback transport and terminal-owner policy. The
// selected draw scope is explicit; later original draws remain unobserved.
import {observeOneNormalFrame} from './one_normal_frame_observation_adapter.mjs';
import {retireNativeFrameEffect} from './match_frame_effect_driver.mjs';
import {armNativeFrameEffectFirstDraw,readNativeFrameEffectFirstDraw,firstDrawObservationFailures,readFirstDrawComponents,armFirstDrawComponents,firstDrawComponentFailures,startFirstDrawSelectedTextureInterval,endFirstDrawSelectedTextureInterval,naturalOneEnrollmentFailures,readFirstDrawHostOwners}
  from './match_frame_effect_first_draw_driver.mjs';
export const firstDrawDriver = Object.freeze({
  armNativeFrameEffect: armNativeFrameEffectFirstDraw,
  readNativeFrameEffect: readNativeFrameEffectFirstDraw,
  retireNativeFrameEffect,
  frameEffectObservationFailures: firstDrawObservationFailures,
});
export const firstDrawComponentsDriver=Object.freeze({...firstDrawDriver,
  readFrameComponents:readFirstDrawComponents,armFrameComponents:armFirstDrawComponents,componentObservationFailures:firstDrawComponentFailures});
// Selection draw is historical input to a later prospective component interval.
// Every actual page response is retained for strict host-helper replay; no tick/resume/restore.
export async function observeSelectedTextureInterval({invoke,components}){
  const transport=[];
  const recordedInvoke=async(fn,arg)=>{
    const row={sequence:transport.length,operation:fn.name,argument:arg??null};transport.push(row);
    try{row.response=await invoke(fn,arg);return row.response;}
    catch(error){row.error=String(error?.message||error);throw error;}
  };
  const started=await startFirstDrawSelectedTextureInterval({invoke:recordedInvoke,components});
  const ended=started.status==='active'?await endFirstDrawSelectedTextureInterval({invoke:recordedInvoke,components,started}):null;
  const failures=[...(started.failures??[]),...(ended?.failures??[])];
  return {schema:'melee-web-selected-texture-interval-observation-v1',status:ended?.status==='closed'&&!failures.length?'closed':'refused',
    composite_eligible:false,started,ended,transport,failures};
}
export const firstDrawSelectedTextureIntervalDriver=Object.freeze({...firstDrawComponentsDriver,observeSelectedTextureInterval,naturalOneEligibility:true,naturalOneEnrollmentFailures});
export async function observeOneFirstDraw(options) {
  const result = await observeOneNormalFrame({...options, driver: options.driver ?? firstDrawDriver, observationScope: 'first_draw'});
  result.schema = 'melee-web-one-first-draw-observation-v1';
  result.scope = 'first_draw_in_normal_callback_observation_only';
  result.later_draws_observed = false;
  if (result.native_retired === true && result.retirement?.status?.renderer_armed === true &&
      result.retirement.status.selected_depth_owners?.finalized !== true) {
    result.status = 'failed';
    result.failures.push('selected_map_observation_not_finalized');
  }
  return result;
}

// Separate point-read record: the selected interval's eight rows stay unchanged.
export function firstDrawHostOwnerFailures(value){
 const exact=(v,keys)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).sort().join(',')===[...keys].sort().join(',');
 const keys=['generation','unresolvedReads','resultCount','discSession','assetTransfer','preparing'];
 const snapshot=v=>exact(v,keys)&&typeof v.generation==='string'&&/^(0|[1-9][0-9]*)$/.test(v.generation)&&
   v.unresolvedReads===0&&v.resultCount===0&&v.discSession===true&&v.assetTransfer===false&&v.preparing===false;
 if(!exact(value,['schema','status','before','after','native_quiescent','same_module','returned_idle','point_observation_only','composite_eligible'])||
    value.schema!=='melee-web-first-draw-host-owners-v1'||value.status!=='observed'||value.native_quiescent!==1||
    value.same_module!==true||value.returned_idle!==true||value.point_observation_only!==true||value.composite_eligible!==false||
    !snapshot(value.before)||!snapshot(value.after)||keys.some(k=>value.before[k]!==value.after[k]))return ['host_owner_point_read_refused_or_malformed'];
 return [];
}
export async function observeSelectedTextureIntervalWithHostRead({invoke,components}){
 const host_transport={sequence:0,operation:'readFirstDrawHostOwners',argument:null};
 try{host_transport.response=await invoke(readFirstDrawHostOwners);}
 catch(error){host_transport.error=String(error?.message||error);return {schema:'melee-web-selected-texture-interval-observation-v1',status:'refused',
   composite_eligible:false,started:null,ended:null,transport:[],host_owners:null,host_transport,failures:['host_owner_point_read_transport_failed']};}
 const host_owners=host_transport.response,failures=firstDrawHostOwnerFailures(host_owners);
 if(failures.length)return {schema:'melee-web-selected-texture-interval-observation-v1',status:'refused',composite_eligible:false,
   started:null,ended:null,transport:[],host_owners,host_transport,failures};
 return {...await observeSelectedTextureInterval({invoke,components}),host_owners,host_transport};
}
export const firstDrawHostOwnersIntervalDriver=Object.freeze({...firstDrawSelectedTextureIntervalDriver,
 hostOwnerPointRead:'first-draw-host-owners-v1',
 observeSelectedTextureInterval:observeSelectedTextureIntervalWithHostRead});

// The getter serializes through diagnostic Wasm storage. It runs only after the
// actual start/end helpers have closed the interval, before observer retirement.
export function readFirstDrawColorConversions(){
 const refuse=reason=>({schema:'melee-web-color-conversion-point-read-v1',status:'refused',reason,composite_eligible:false});
 try{
  const module=globalThis.Module;
  const getter=module?._melee_web_native_menu_frame_effect_color_conversions;
  const stable=()=>globalThis.Module===module&&globalThis.__meleeWebFirstDrawComponentModuleV1===module&&
   typeof module?.matchFrameEffectNativeIdle==='function'&&module.matchFrameEffectNativeIdle()===true&&
   module.matchFrameEffectCallbackReturned===true&&typeof module._melee_web_native_menu_running==='function'&&module._melee_web_native_menu_running()===0;
  if(!stable())return refuse('same_module_or_returned_idle_missing');
  if(typeof getter!=='function'||typeof module.UTF8ToString!=='function')return refuse('conversion_reader_missing');
  const raw=module.UTF8ToString(getter());
  if(!stable()||module._melee_web_native_menu_frame_effect_color_conversions!==getter)return refuse('module_or_reader_changed');
  if(typeof raw!=='string')return refuse('conversion_raw_missing');
  const companion=JSON.parse(raw);
  return {schema:'melee-web-color-conversion-point-read-v1',status:'observed',raw,companion,
   same_module:true,returned_idle:true,diagnostic_wasm_serialization:true,composite_eligible:false};
 }catch{return refuse('conversion_reader_failed');}
}
export async function observeHostIntervalThenColorConversions({invoke,components,packet}){
 const interval=await observeSelectedTextureIntervalWithHostRead({invoke,components});
 if(interval.status!=='closed')return interval;
 // Reuse the actual helpers' closed result; no second fence, replay dispatch or
 // interval validation engine. The eight original response rows remain intact.
 if(interval.started?.status!=='active'||interval.ended?.status!=='closed'||interval.failures.length||interval.transport.length!==8)
  return {...interval,status:'refused',failures:['closed_eight_response_interval_required']};
 const transport={sequence:0,operation:'readFirstDrawColorConversions',argument:null};
 let point;
 try{transport.response=await invoke(readFirstDrawColorConversions);point=transport.response;}
 catch(error){transport.error=String(error?.message||error);return {...interval,status:'refused',color_conversion_point:{transport},failures:['conversion_point_transport_failed']};}
 const exact=v=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).sort().join(',')===
  'companion,composite_eligible,diagnostic_wasm_serialization,raw,returned_idle,same_module,schema,status';
 let failures=[],dependencies=null;
 try{
  if(!exact(point)||point.schema!=='melee-web-color-conversion-point-read-v1'||point.status!=='observed'||
    point.same_module!==true||point.returned_idle!==true||point.diagnostic_wasm_serialization!==true||point.composite_eligible!==false||
    typeof point.raw!=='string'||JSON.stringify(JSON.parse(point.raw))!==JSON.stringify(point.companion))failures.push('conversion_point_refused_or_malformed');
  else {dependencies=readColorConversionDependencies(packet,point.companion);failures.push(...dependencies.failures);if(dependencies.dependenciesObserved!==true)failures.push('conversion_dependencies_not_observed');}
 }catch{failures.push('conversion_point_decoder_failed');}
 return {...interval,status:failures.length?'refused':interval.status,
  color_conversion_point:{transport,dependencies,after_closed_interval:true,diagnostic_wasm_serialization:true,composite_eligible:false},failures};
}
export const firstDrawHostOwnersColorConversionsIntervalDriver=Object.freeze({...firstDrawHostOwnersIntervalDriver,
 observeSelectedTextureInterval:observeHostIntervalThenColorConversions});
