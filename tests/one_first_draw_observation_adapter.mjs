// Reuse the bounded normal-callback transport and terminal-owner policy. The
// selected draw scope is explicit; later original draws remain unobserved.
import {observeOneNormalFrame} from './one_normal_frame_observation_adapter.mjs';
import {retireNativeFrameEffect} from './match_frame_effect_driver.mjs';
import {armNativeFrameEffectFirstDraw,readNativeFrameEffectFirstDraw,firstDrawObservationFailures,readFirstDrawComponents,armFirstDrawComponents,firstDrawComponentFailures,startFirstDrawSelectedTextureInterval,endFirstDrawSelectedTextureInterval}
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
export const firstDrawSelectedTextureIntervalDriver=Object.freeze({...firstDrawComponentsDriver,observeSelectedTextureInterval});
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
