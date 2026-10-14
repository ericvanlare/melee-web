// Reuse the bounded normal-callback transport and terminal-owner policy. The
// selected draw scope is explicit; later original draws remain unobserved.
import {observeOneNormalFrame} from './one_normal_frame_observation_adapter.mjs';
import {retireNativeFrameEffect} from './match_frame_effect_driver.mjs';
import {armNativeFrameEffectFirstDraw,readNativeFrameEffectFirstDraw,firstDrawObservationFailures}
  from './match_frame_effect_first_draw_driver.mjs';
export const firstDrawDriver = Object.freeze({
  armNativeFrameEffect: armNativeFrameEffectFirstDraw,
  readNativeFrameEffect: readNativeFrameEffectFirstDraw,
  retireNativeFrameEffect,
  frameEffectObservationFailures: firstDrawObservationFailures,
});
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
