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
