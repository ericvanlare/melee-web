import {validateRngDrawProbeRows} from './rng_draw_probe.mjs';
const ID='original-b-5422-951-long-v1';
export function deriveLongBActionRngWindow(spec,derived) {
  const p=derived?.reference,selector=p?.action_rng?.selector;
  if(spec!=='actions'||p?.reference_id!==ID || derived.goTick!==84 || p.actions?.length!==7168 ||
    JSON.stringify(selector)!==JSON.stringify({schema:'melee-web-later-b-long-action-rng-v1',reference_id:ID,
      prefix_cursor:951,reference_go_tick:84,first_action:168,last_action:7167}) || p.action_rng.rows?.length!==7000)
    throw Error('Long B RNG requires its exact actual action selector');
  const selected=Array.from({length:7000},(_,i)=>168+i);
  for(const [i,row] of p.action_rng.rows.entries()) {
    const a=p.actions[selected[i]],coordinate={source_tick:a.source.source_tick,draw_ordinal:a.source.draw_ordinal};
    if(row.action_index!==selected[i] || row.kind!==a.kind || JSON.stringify(row.source_coordinate)!==JSON.stringify(coordinate))
      throw Error('Long B RNG projection is not its actual action');
  }
  return {configuration:selector,selection:{request:{kind:'cursors',cursors:selected},selected},coordinates:p.action_rng.rows};
}
// The legacy source_cursor field now carries a diagnostic action index only.
// Match every raw row to the unmodified completed native action receipt before
// labeling its source coordinates. Never infer them from one-tick/one-draw pairs.
export function bindLongBActionRngRows(textRows,window,receipts,{complete=true}={}) {
  const last=textRows.length?JSON.parse(textRows.at(-1)).source_cursor:167;
  const validation=validateRngDrawProbeRows(textRows,window.selection,
    complete?{}:{deliberateStop:{requested_cursor:last}});
  const rows=validation.rows.map(text=>{
    const probe=JSON.parse(text),index=probe.source_cursor,expected=window.coordinates[index-168],r=receipts.get(index)?.last_action;
    if(probe.version!==3 || !expected || !r || r.complete!==true || r.schedule_index!==index ||
      r.action!==expected.kind || r.rng_action_scope!=='diagnostic-action-index' || r.rng_action_index!==index ||
      r.source_ticks_before!==expected.source_coordinate.source_tick ||
      r.source_draws_before!==expected.source_coordinate.draw_ordinal)
      throw Error('Long B RNG raw row lacks its exact completed native action owner');
    return {schema:'melee-web-stadium-long-b-action-rng-row-v1',reference_id:ID,
      action_index:index,kind:r.action,source_coordinate:{source_tick:r.source_ticks_before,draw_ordinal:r.source_draws_before},probe};
  });
  return {...validation,rows};
}

// Serialized into the page after the checked zero-tick Setup reservation.
// Configuration was cached by C with an empty cursor array during menus.
export function activateLongBActionRngSelection({configuration,selected}) {
  const want={schema:'melee-web-later-b-long-action-rng-v1',reference_id:'original-b-5422-951-long-v1',
    prefix_cursor:951,reference_go_tick:84,first_action:168,last_action:7167};
  if(JSON.stringify(configuration)!==JSON.stringify(want) ||
    JSON.stringify(window.__meleeLaterBRngOwnerWindow)!==JSON.stringify(want) ||
    window.__meleeRngDrawProbeOwners!==true || window.__meleeRngDrawProbeFighterOwners!==true ||
    !Array.isArray(window.__meleeRngDrawProbeCursors) || window.__meleeRngDrawProbeCursors.length!==0 ||
    window.__meleeRngDrawProbeRange!==null || !Array.isArray(window.__rngDrawProbeRows) || window.__rngDrawProbeRows.length!==0 ||
    !Array.isArray(selected) || selected.length!==7000 || selected.some((n,i)=>n!==168+i))
    throw Error('Long B action RNG activation lost its empty pre-arm selection');
  window.__meleeRngDrawProbeCursors=Object.freeze([...selected]);
  return {activated:true,first_action:168,last_action:7167};
}
