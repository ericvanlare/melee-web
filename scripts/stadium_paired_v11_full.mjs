// Pure comparisons consume actual native stream rows; no source commands or ACKs.
import {typedOriginalValues,firstFormCommonOriginalValues,firstWaterCommonOriginalValues,compareLaterBGate2Go} from '../tests/stadium_matched_setup_cf_consumer.mjs';
import {validateRngDrawProbeRows} from './rng_draw_probe.mjs';
const need=(ok,why)=>{if(!ok)throw Error('Paired full comparison: '+why);};
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const seed=value=>typeof value==='string'&&/^[0-9a-f]{8}$/.test(value);
export const FIRST_FIRE_STATE_KEYS=Object.freeze(['map2_countdown','map2_phase','map2_form','map2_history_e0','map2_history_e2','map2_gate','display_mode','display_e6','display_e8','display_previous_mode','display_selected_player','display_f2','display_flags','map2_pending']);
export function firstFireOriginalValues(fields,sourceTick){
 const values=firstFormCommonOriginalValues(fields,sourceTick),pending=fields.map2.gate_mask_40;
 need(typeof pending==='boolean'&&((Number.parseInt(fields.map2.gate_raw_hex,16)&0x40)!==0)===pending,'first-fire actual pending bit');
 values.map2_pending=pending;
 need(same(Object.keys(values),FIRST_FIRE_STATE_KEYS),'first-fire ordered state field contract');
 return values;
}
export const FIRST_WATER_STATE_KEYS=Object.freeze([...FIRST_FIRE_STATE_KEYS]);
export function firstWaterOriginalValues(fields,sourceTick){
 const values=firstWaterCommonOriginalValues(fields,sourceTick),pending=fields.map2.gate_mask_40;
 need(typeof pending==='boolean'&&((Number.parseInt(fields.map2.gate_raw_hex,16)&0x40)!==0)===pending,'first-water actual pending bit');
 values.map2_pending=pending;
 need(same(Object.keys(values),FIRST_WATER_STATE_KEYS),'first-water ordered state field contract');
 return values;
}
const registrationOfflineScope='original-paired-registration-offline-postflight-v1';
const registrationRawRoles=['report.json','launch.json','native-primary.mwro','native-primary-status.json',
 'native-inputs.mwri','native-input-status.json','native-allocations.jsonl','allocation-profile.json',
 'stadium-whole-paired-compact-rng.mwrl','cleanup.json','input-intentions.jsonl'];
export function derivePairedFull(p,recipeSha,frames,endpoints){
 if(p?.schema==='melee-web-stadium-first-water-pair-projection-v1')return deriveFirstWaterPair(p,recipeSha,frames,endpoints);
 if(p?.schema==='melee-web-stadium-first-fire-pair-projection-v1')return deriveFirstFirePair(p,recipeSha,frames,endpoints);
 if(p?.schema==='melee-web-stadium-natural-mode14-pair-projection-v1')return deriveNaturalMode14Pair(p,recipeSha,frames,endpoints);
 if(p?.schema==='melee-web-stadium-stockout-pair-projection-v1')return deriveStockoutPair(p,recipeSha,frames,endpoints);
 need(p?.schema==='melee-web-stadium-paired-window-projection-v1'&&p.source_schedule_verified===true&&p.recipe_sha256===recipeSha&&p.actual_frame_count===frames&&p.matches?.length===2,'projection/recipe identity');
 const registration=p.capture_profile==='paired_idle_registration_v1';
 need(p.capture_profile===undefined||registration,'unknown original capture profile');
 if(registration){
  if(endpoints?.scope===registrationOfflineScope){
   need(endpoints.result==='PASS_OFFLINE_POSTFLIGHT_REFERENCE_ONLY'&&endpoints.original_live_result==='fail'&&
    endpoints.inputs_unchanged===true&&endpoints.native_capture_repeated===false&&
    endpoints.original_operation_deadline_reclassified===false&&endpoints.browser_parity===false&&
    endpoints.C3===false&&endpoints.native_pid_absent===true&&endpoints.native_group_absent===true,
    'registration original offline postflight scope');
   need(p.postflight_scope===registrationOfflineScope&&p.original_live_result==='fail',
    'registration projection offline provenance differs');
   for(const name of registrationRawRoles){
    const identity=p.source_files?.[name],observed=endpoints.inputs?.[name];
    need(identity&&Number.isSafeInteger(identity.bytes)&&identity.bytes>0&&
     /^[0-9a-f]{64}$/.test(identity.sha256||'')&&observed?.bytes===identity.bytes&&
     observed.sha256===identity.sha256,'registration offline raw role '+name);
   }
  }else need(endpoints?.result==='pass'&&endpoints.scope==='original-stadium-paired-registration-v1'&&Array.isArray(endpoints.cleanup_errors)&&endpoints.cleanup_errors.length===0,'registration original live result');
  need(['native-primary.mwro','native-allocations.jsonl','native-inputs.mwri'].every(name=>/^[0-9a-f]{64}$/.test(p.source_files?.[name]?.sha256||'')),'registration source hashes missing');
  need(endpoints.input_stream?.complete===true&&endpoints.input_stream.sha256===p.source_files?.['native-inputs.mwri']?.sha256,'registration literal input producer differs');
  need(typeof endpoints.native?.native_allocations_sha256==='string'&&endpoints.native.native_allocations_sha256===p.source_files?.['native-allocations.jsonl']?.sha256,'registration allocation producer differs');
 }else need(endpoints?.result==='PASS_OFFLINE_POSTFLIGHT_REFERENCE_ONLY'&&endpoints.inputs_unchanged===true,'original endpoint postflight');
 need(endpoints.native?.primary_sha256===p.source_files?.['native-primary.mwro']?.sha256,'endpoint primary producer differs');
 for(const [name,identity]of Object.entries(p.source_files||{})){const other=endpoints.inputs?.[name];if(other)need(other.sha256===identity.sha256&&other.bytes===identity.bytes,'endpoint source role '+name);}
 const points=endpoints.native?.original_rng_scene_boundaries;
 need(points?.observed===true&&points.sample_count===8&&points.samples?.length===8,'eight original scene endpoints');
 let total=0;
 const matches=p.matches.map((m,i)=>{
  need(m.match_index===i&&m.states?.filter(s=>s.selected).length===3501&&m.actions?.length>0&&m.window?.state_samples===3501,'two actual bounded windows');
  need(m.setup?.declared_setup?.stage===3&&m.setup.declared_setup.players?.length===2&&m.setup.declared_setup.players.every(x=>x.character_kind===8&&x.player_type===0&&x.stocks===4),'exact two-human Mario Stadium setup');
  m.actions.forEach((a,j)=>need(a.action_index===j&&['tick','draw'].includes(a.kind)&&Number.isSafeInteger(a.source_tick)&&Number.isSafeInteger(a.draw_ordinal),'actual action order'));
  const states=new Map(m.states.map(s=>[s.tick.source_tick,s]));need(states.size===m.states.length,'duplicate typed tick');
  const ep=points.samples.slice(i*4,i*4+4);
  ep.forEach((x,j)=>need(x.lifetime===i+1&&x.function===(j<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit')&&x.phase===(j%2?'return':'enter')&&Number.isInteger(x.value)&&x.value>=0&&x.value<=0xffffffff,'original endpoint order'));
  const result={reference:m,offset:total,states,endpoints:ep};total+=m.actions.length;return result;
 });
 need(total>0&&total<=2*frames,'bounded actual action extent');
 return {projection:p,matches,total,selection:{request:{kind:'range',first:0,last:total-1},selected:Array.from({length:total},(_,i)=>i)}};
}
function deriveStockoutPair(p,recipeSha,frames,endpoints){
 need(p.profile==='selected_b_natural_stockout_v1'&&p.source_schedule_verified===true&&p.recipe_sha256===recipeSha&&p.actual_frame_count===frames&&p.matches?.length===2,'stockout projection/recipe identity');
 need(endpoints?.result==='pass'&&endpoints.scope==='original-stadium-whole3-natural-stockout-v1'&&endpoints.native?.primary_sha256===p.source_files?.['native-primary.mwro']?.sha256,'stockout original producer/result');
 const points=endpoints.native.original_rng_scene_boundaries;
 need(points?.observed===true&&points.sample_count===8&&points.samples?.length===8,'stockout eight scene endpoints');
 let total=0;
 const matches=p.matches.map((m,i)=>{
  need(m.match_index===i&&m.actions?.length>0&&Array.isArray(m.states)&&m.window?.state_samples===m.states.length,'stockout observed scope');
  need(m.setup?.declared_setup?.stage===3&&m.setup.declared_setup.players?.length===2&&m.setup.declared_setup.players.every(x=>x.character_kind===8&&x.player_type===0&&x.stocks===4),'stockout exact setup');
  m.actions.forEach((a,j)=>need(a.action_index===j&&['tick','draw'].includes(a.kind)&&Number.isSafeInteger(a.source_tick)&&Number.isSafeInteger(a.draw_ordinal),'stockout actual action order'));
  need(i===0?m.states.length===0&&m.go===null&&m.constructor===null:m.states.length>0&&m.states.every(s=>s.selected===true),'stockout A/B observation availability');
  const states=new Map(m.states.map(s=>[s.tick.source_tick,s]));need(states.size===m.states.length,'stockout duplicate typed tick');
  if(i===1){
   need(m.states.length===endpoints.stadium_whole_stockout?.source_tick_count&&m.states[0].tick.source_tick===endpoints.stadium_whole_stockout.go_source_tick,'stockout source window extent');
   const stocks=m.states.map(s=>s.players?.map(x=>x.stocks));
   need(stocks.every(x=>x?.length===2&&x[1]===4)&&same(stocks[0],[4,4])&&same(stocks.at(-1),[0,4]),'stockout observed four-stock endpoints');
   const losses=stocks.slice(1).filter((x,j)=>x[0]!==stocks[j][0]).map(x=>x[0]);need(same(losses,[3,2,1,0]),'stockout exact decrements');
   need(m.states.some(s=>s.state.display.mode_s16===9&&same(s.players.map(x=>x.stocks),[0,4])),'stockout mode9 with zero stocks');
  }
  const ep=points.samples.slice(i*4,i*4+4);
  ep.forEach((x,j)=>need(x.lifetime===i+1&&x.function===(j<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit')&&x.phase===(j%2?'return':'enter')&&Number.isInteger(x.value)&&x.value>=0&&x.value<=0xffffffff,'stockout endpoint order'));
  const result={reference:m,offset:total,states,endpoints:ep};total+=m.actions.length;return result;
 });
 need(total>0&&total<=2*frames,'stockout actual action extent');
 return {projection:p,matches,total,stockout:true,selection:{request:{kind:'range',first:0,last:total-1},selected:Array.from({length:total},(_,i)=>i)}};
}
function sameNatural14WitnessFields(a,b){
 if(!a||!b||Array.isArray(a)||Array.isArray(b))return false;
 const keys=Reflect.ownKeys(a);return keys.length===Reflect.ownKeys(b).length&&keys.every(k=>Object.hasOwn(b,k)&&(a[k]===null||typeof a[k]!=='object')&&Object.is(a[k],b[k]));
}
function deriveFirstFirePair(p,recipeSha,frames,endpoints){
 need(p.profile==='selected_b_first_fire_recorded_callback_v1'&&p.source_schedule_verified===true&&
  p.recipe_sha256===recipeSha&&p.actual_frame_count===frames&&p.matches?.length===2,'first-fire exact recipe/profile');
 need(endpoints?.result==='pass'&&endpoints.scope==='original-stadium-whole3-first-form-latency-record-v2'&&
  endpoints.cleanup_errors?.length===0&&endpoints.native?.primary_sha256===p.source_files?.['native-primary.mwro']?.sha256,
  'first-fire original full3 producer');
 need(p.claims?.per_owner_rng_comparison===false&&p.claims?.C4_complete===false&&p.original_route==='whole3'&&
  p.runtime_view==='first_two_complete_lifetimes','first-fire declared derived scope');
 const t=endpoints.stadium_selected_request_timing,f=p.first_fire;
 need(t?.schema==='melee-web-stadium-selected-request-source-timing-summary-v1'&&
  t.profile==='first_selected_stage_request_v1'&&t.identity?.selected_form===3&&
  t.drive_model_observed===false&&t.C4_complete===false&&Array.isArray(t.rows),'first-fire actual request clocks');
 const request=t.rows.filter(r=>r.kind==='request_enter'),callback=t.rows.filter(r=>r.kind==='callback_enter'),
  polls=t.rows.filter(r=>r.kind==='poll_return'&&r.result===1);
 need(request.length===1&&callback.length===1&&polls.length===1&&f?.selected_form===3&&
  sameNatural14WitnessFields(f.request,request[0])&&sameNatural14WitnessFields(f.callback,callback[0])&&
  sameNatural14WitnessFields(f.successful_poll,polls[0]),'first-fire exact clock rows');
 const last=endpoints.stadium_whole_first_form_latency?.first_form_latency?.settled_phase0;
 need(last?.selected_form===3&&last.phase===0&&last.active_map_id===3&&last.root_replacement_observed===true&&
  sameNatural14WitnessFields(f.settled,last),'first-fire actual source-created settled root');
 need([request[0].source_tick,callback[0].source_tick,last.source_tick].every(x=>Number.isSafeInteger(x)&&x>=0&&x<=0xffffffff)&&
  request[0].source_tick<callback[0].source_tick&&callback[0].source_tick<last.source_tick,'first-fire bounded coordinates');
 const points=endpoints.native.original_rng_scene_boundaries;
 need(points?.observed===true&&points.sample_count===8&&points.samples?.length===8,'first-fire eight source endpoints');
 let total=0;
 const matches=p.matches.map((r,i)=>{
  need(r.match_index===i&&r.actions?.length>0&&Array.isArray(r.states)&&r.window?.state_samples===r.states.length&&
   r.window.natural_tail_ticks===0,'first-fire match/action/state scope');
  need(r.setup?.declared_setup?.stage===3&&r.setup.declared_setup.players?.length===2&&
   r.setup.declared_setup.players.every(x=>x.character_kind===8&&x.player_type===0&&x.stocks===4),'first-fire original setup');
  r.actions.forEach((a,j)=>need(a.action_index===j&&['tick','draw'].includes(a.kind)&&
   Number.isSafeInteger(a.source_tick)&&Number.isSafeInteger(a.draw_ordinal)&&Number.isSafeInteger(a.frame_index)&&
   a.after_sequence<a.through_sequence,'first-fire exact action coordinates'));
  if(i===0)need(!r.states.length&&r.go===null&&r.constructor===null,'first-fire unpaired A fields');
  else{
   need(r.states.length===last.source_tick-r.window.go_tick+1&&r.go&&r.constructor&&
    r.states.every((x,n)=>x.selected===true&&x.tick.source_tick===r.window.go_tick+n),'first-fire contiguous original window');
   r.states.forEach(x=>{const v=firstFormCommonOriginalValues(x.state,x.tick.source_tick);
    need(v.map2_form===5||v.map2_form===3,'first-fire foreign form');});
   const final=firstFormCommonOriginalValues(r.states.at(-1).state,last.source_tick);
   need(final.map2_phase===0&&final.map2_form===3&&r.window.last_selected_source_tick===last.source_tick,
    'first-fire selected last state');
  }
  const ep=points.samples.slice(i*4,i*4+4);
  ep.forEach((x,j)=>need(x.lifetime===i+1&&x.function===(j<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit')&&
   x.phase===(j%2?'return':'enter')&&Number.isInteger(x.value)&&x.value>=0&&x.value<=0xffffffff,'first-fire scene endpoint order'));
  const result={reference:r,offset:total,states:new Map(r.states.map(x=>[x.tick.source_tick,x])),endpoints:ep};
  need(result.states.size===r.states.length,'first-fire duplicate state');total+=r.actions.length;return result;
 });
 need(total>0&&total<=2*frames,'first-fire bounded action extent');
 return {projection:p,matches,total,firstFire:true,selection:{request:{kind:'range',first:0,last:total-1},selected:Array.from({length:total},(_,i)=>i)}};
}
function deriveFirstWaterPair(p,recipeSha,frames,endpoints){
 need(p.profile==='selected_b_first_water_recorded_callback_v1'&&p.source_schedule_verified===true&&
  p.recipe_sha256===recipeSha&&p.actual_frame_count===frames&&p.matches?.length===2,'first-water exact recipe/profile');
 need(endpoints?.result==='pass'&&endpoints.scope==='original-stadium-whole3-first-form-latency-replay-v1'&&
  endpoints.cleanup_errors?.length===0&&endpoints.native?.primary_sha256===p.source_files?.['native-primary.mwro']?.sha256,
  'first-water original full3 producer');
 need(p.claims?.per_owner_rng_comparison===false&&p.claims?.C4_complete===false&&p.original_route==='whole3'&&
  p.runtime_view==='first_two_complete_lifetimes','first-water declared derived scope');
 const replay=endpoints.stadium_whole_first_form_latency_replay,join=replay?.sequence_correspondence;
 need(replay?.schema==='melee-web-stadium-first-form-latency-replay-comparison-v1'&&
  replay.comparison_result==='PASS_EXACT_INPUT_AND_FIRST_FORM_SAMPLE'&&replay.input_terminal_checks==='PASS'&&
  replay.summary_equal===true&&join?.raw_rows_modified===false&&
  Number.isSafeInteger(join.original_rows)&&join.original_rows>0&&
  Number.isSafeInteger(join.actual_rows)&&join.actual_rows>join.original_rows&&
  Number.isSafeInteger(join.timing_rows)&&join.timing_rows>0&&
  join.original_rows+join.timing_rows===join.actual_rows&&
  Array.isArray(join.original_to_actual)&&join.original_to_actual.length===join.original_rows&&
  Array.isArray(join.timing_actual_sequences)&&join.timing_actual_sequences.length===join.timing_rows,
  'first-water exact-input replay and immutable raw sequence correspondence');
 const t=endpoints.stadium_selected_request_timing,f=p.first_water;
 need(t?.schema==='melee-web-stadium-selected-request-source-timing-summary-v1'&&
  t.profile==='first_selected_stage_request_v1'&&t.identity?.selected_form===9&&
  t.drive_model_observed===false&&t.C4_complete===false&&Array.isArray(t.rows),'first-water actual request clocks');
 const request=t.rows.filter(r=>r.kind==='request_enter'),callback=t.rows.filter(r=>r.kind==='callback_enter'),
  polls=t.rows.filter(r=>r.kind==='poll_return'&&r.result===1);
 need(request.length===1&&callback.length===1&&polls.length===1&&f?.selected_form===9&&
  sameNatural14WitnessFields(f.request,request[0])&&sameNatural14WitnessFields(f.callback,callback[0])&&
  sameNatural14WitnessFields(f.successful_poll,polls[0]),'first-water exact clock rows');
 const last=replay.actual_summary?.first_form_latency?.settled_phase0;
 need(last?.selected_form===9&&last.phase===0&&last.active_map_id===9&&last.root_replacement_observed===true&&
  sameNatural14WitnessFields(f.settled,last),'first-water actual source-created settled root');
 need([request[0].source_tick,callback[0].source_tick,last.source_tick].every(x=>Number.isSafeInteger(x)&&x>=0&&x<=0xffffffff)&&
  request[0].source_tick<callback[0].source_tick&&callback[0].source_tick<last.source_tick,'first-water bounded coordinates');
 const points=endpoints.native.original_rng_scene_boundaries;
 need(points?.observed===true&&points.sample_count===8&&points.samples?.length===8,'first-water eight source endpoints');
 let total=0;
 const matches=p.matches.map((r,i)=>{
  need(r.match_index===i&&r.actions?.length>0&&Array.isArray(r.states)&&r.window?.state_samples===r.states.length&&
   r.window.natural_tail_ticks===0,'first-water match/action/state scope');
  need(r.setup?.declared_setup?.stage===3&&r.setup.declared_setup.players?.length===2&&
   r.setup.declared_setup.players.every(x=>x.character_kind===8&&x.player_type===0&&x.stocks===4),'first-water original setup');
  r.actions.forEach((a,j)=>need(a.action_index===j&&['tick','draw'].includes(a.kind)&&
   Number.isSafeInteger(a.source_tick)&&Number.isSafeInteger(a.draw_ordinal)&&Number.isSafeInteger(a.frame_index)&&
   a.after_sequence<a.through_sequence,'first-water exact action coordinates'));
  if(i===0)need(!r.states.length&&r.go===null&&r.constructor===null,'first-water unpaired A fields');
  else{
   need(r.states.length===last.source_tick-r.window.go_tick+1&&r.go&&r.constructor&&
    r.states.every((x,n)=>x.selected===true&&x.tick.source_tick===r.window.go_tick+n),'first-water contiguous original window');
   r.states.forEach(x=>{const v=firstWaterCommonOriginalValues(x.state,x.tick.source_tick);
    need(v.map2_form===5||v.map2_form===9,'first-water foreign form');});
   const final=firstWaterCommonOriginalValues(r.states.at(-1).state,last.source_tick);
   need(final.map2_phase===0&&final.map2_form===9&&r.window.last_selected_source_tick===last.source_tick,
    'first-water selected last state');
  }
  const ep=points.samples.slice(i*4,i*4+4);
  ep.forEach((x,j)=>need(x.lifetime===i+1&&x.function===(j<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit')&&
   x.phase===(j%2?'return':'enter')&&Number.isInteger(x.value)&&x.value>=0&&x.value<=0xffffffff,'first-water scene endpoint order'));
  const result={reference:r,offset:total,states:new Map(r.states.map(x=>[x.tick.source_tick,x])),endpoints:ep};
  need(result.states.size===r.states.length,'first-water duplicate state');total+=r.actions.length;return result;
 });
 need(total>0&&total<=2*frames,'first-water bounded action extent');
 return {projection:p,matches,total,firstWater:true,selection:{request:{kind:'range',first:0,last:total-1},selected:Array.from({length:total},(_,i)=>i)}};
}
export function validateFirstFireCompletion(c,f){
 need(c?.schema==='melee-web-stadium-first-fire-recorded-completion-v1'&&
  c.armed===1&&c.failed===0&&c.completed===1&&c.request_seen===1&&
  Number.isSafeInteger(c.local_request_id)&&c.local_request_id>0&&
  c.actual_request_cursor===f.request.source_tick&&c.actual_callback_cursor===f.callback.source_tick&&
  Number.isSafeInteger(c.last_preconsume_cursor)&&c.last_preconsume_cursor>=f.settled.source_tick,
  'first-fire recorded completion receipt');
 return true;
}
export function validateFirstWaterCompletion(c,f){
 need(c?.schema==='melee-web-stadium-first-water-recorded-completion-v1'&&
  c.armed===1&&c.failed===0&&c.completed===1&&c.request_seen===1&&
  Number.isSafeInteger(c.local_request_id)&&c.local_request_id>0&&
  c.actual_request_cursor===f.request.source_tick&&c.actual_callback_cursor===f.callback.source_tick&&
  Number.isSafeInteger(c.last_preconsume_cursor)&&c.last_preconsume_cursor>=f.settled.source_tick,
  'first-water recorded completion receipt');
 return true;
}
function validateFirstFireLocalGraph(g,generation,terminal=false){
 const sequence=[[1,3],[2,5],[3,5],[2,3],[3,3]];
 need(g?.schema==='melee-web-stadium-first-fire-native-graph-v1'&&g.world_generation===generation&&
  g.armed===(terminal?2:1)&&g.failed===false&&g.scalar_scope==='last_live_validated_sample'&&
  g.scalar_sample_valid===true&&Number.isSafeInteger(g.scalar_world_ticks)&&g.scalar_world_ticks>0&&
  Number.isInteger(g.event_count)&&g.event_count>=0&&g.event_count<=(terminal?5:3)&&
  Array.isArray(g.events)&&g.events.length===g.event_count,'first-fire local graph receipt');
 g.events.forEach((e,i)=>need(e.kind===sequence[i][0]&&e.map_id===sequence[i][1],'first-fire source event order'));
 if(terminal)need(g.event_count===5&&g.phase===0&&g.form===3&&g.buffer_uses===2,'first-fire sealed native lifetime');
}
export function validateFirstWaterLocalGraph(g,generation,terminal=false){
 const sequence=[[1,9],[1,7],[1,8],[2,5],[3,5],[2,9],[2,7],[3,7],[2,8],[3,8],[3,9]];
 need(g?.schema==='melee-web-stadium-first-water-native-graph-v1'&&g.selected_form===9&&g.world_generation===generation&&
  g.armed===(terminal?2:1)&&g.failed===false&&g.scalar_scope==='last_live_validated_sample'&&
  g.scalar_sample_valid===true&&Number.isSafeInteger(g.scalar_world_ticks)&&g.scalar_world_ticks>0&&
  Number.isInteger(g.event_count)&&g.event_count>=0&&g.event_count<=(terminal?11:5)&&
  Array.isArray(g.events)&&g.events.length===g.event_count,'first-water local graph receipt');
 g.events.forEach((e,i)=>need(e.kind===sequence[i][0]&&e.map_id===sequence[i][1],'first-water source event order'));
 if(terminal)need(g.event_count===11&&g.phase===0&&g.form===9&&g.buffer_uses===2,'first-water sealed native lifetime');
}
function deriveNaturalMode14Pair(p,recipeSha,frames,endpoints){
 need(p?.profile==='selected_b_natural_mode14_v1'&&p.source_schedule_verified===true&&p.recipe_sha256===recipeSha&&p.actual_frame_count===frames&&p.matches?.length===2,'natural14 recipe/profile');
 need(endpoints?.result==='pass'&&endpoints.scope==='original-stadium-whole3-post-transform-mode14-input-v1'&&endpoints.native?.primary_sha256===p.source_files?.['native-primary.mwro']?.sha256,'natural14 actual collector provenance');
 need(p.source_posttransform_outcome==='INCONCLUSIVE_MODE14_BEFORE_TRANSFORM'&&p.claims?.per_owner_rng_comparison===false&&p.claims?.C3===false,'natural14 scoped outcome');
 need(p.scene_rng_endpoints?.observed===true&&p.scene_rng_endpoints.sample_count===8&&same(p.scene_rng_endpoints,endpoints.native.original_rng_scene_boundaries),'natural14 eight scene endpoints');
 let total=0;
 const matches=p.matches.map((r,i)=>{
  need(r.match_index===i&&r.actions?.length>0&&Array.isArray(r.states)&&r.window?.state_samples===r.states.length,'natural14 match/state inventory');
  need(r.setup?.declared_setup?.stage===3&&r.setup.declared_setup.players?.length===2&&r.setup.declared_setup.players.every(x=>x.character_kind===8&&x.player_type===0&&x.stocks===4),'natural14 exact setup');
  r.actions.forEach((a,j)=>need(a.action_index===j&&['tick','draw'].includes(a.kind)&&Number.isInteger(a.source_tick)&&Number.isInteger(a.draw_ordinal)&&Number.isInteger(a.frame_index)&&a.after_sequence<a.through_sequence,'natural14 source action coordinate'));
  const statesMap=new Map(r.states.map(x=>[x.tick.source_tick,x]));need(statesMap.size===r.states.length,'natural14 duplicate state');
  if(i===0)need(r.states.length===0&&r.go===null&&r.constructor===null,'natural14 unpaired warmup state');
  else{
   need(r.go?.coordinate?.source_tick===r.window.go_tick&&r.states.length===3644&&r.mode14_source_tick===3725&&r.last_selected_source_tick===3727,'natural14 selected extent');
   r.states.forEach((x,j)=>need(x.state.map2.phase_s16===0&&x.state.map2.form_s16===5&&x.state.map2.previous_form_s16===-1&&x.state.map2.prior_form_s16===-1&&x.selected===true&&x.tick.source_tick===r.window.go_tick+j&&same(x.pads,r.actions[x.action_index]?.pads)&&r.actions[x.action_index]?.through_sequence===x.tick.sequence,'natural14 state action/PAD join'));
   const w=r.standings_witness;need(w?.event_stop==='mode14_before_transform'&&w.first_transform===null&&w.neutral_release_observed===true&&same(w,endpoints.stadium_whole_post_transform_standings),'natural14 actual witness');
   const m=statesMap.get(r.mode14_source_tick);need(m.state.display.mode_s16===14&&m.state.map2.phase_s16===0&&m.state.map2.form_s16===5&&m.state.map2.countdown_s32===148&&sameNatural14WitnessFields(m.state.display,w.mode14_witness.display)&&sameNatural14WitnessFields(m.state.map2,w.mode14_witness.map2),'natural14 typed witness');
   need(same(r.states.at(-1).pads,['0000000000000000000000','0000000000000000000000','00000000000000000000ff','00000000000000000000ff']),'natural14 actual neutral release');
   need(r.fixed_native_observation_window?.comparison_typed_state_count===3501&&same(r.fixed_native_observation_window,endpoints.stadium_whole_long_idle_window),'natural14 separate fixed window');
  }
  const ep=p.scene_rng_endpoints.samples.filter(x=>x.lifetime===i+1);need(ep.length===4&&same(ep.map(x=>[x.function,x.phase]),[['gm_Scene_Vs_OnEnter','enter'],['gm_Scene_Vs_OnEnter','return'],['gm_Scene_Vs_OnExit','enter'],['gm_Scene_Vs_OnExit','return']]),'natural14 endpoint order');
  need(r.result?.outcome_byte===7,'natural14 original NoContest');const start=total;total+=r.actions.length;return {reference:r,states:statesMap,offset:start,endpoints:ep};
 });need(total<=2*frames,'natural14 bounded schedule');return {projection:p,matches,total,natural14:true,selection:{request:{kind:'range',first:0,last:total-1},selected:Array.from({length:total},(_,i)=>i)}};
}
export function installPairedFullLaunch({stockout=false,natural14=false,firstFire=false,firstWater=false,requestSourceTick=null,callbackSourceTick=null,mode14SourceTick=null,lastSelectedSourceTick=null}={}){
 if(typeof stockout!=='boolean'||typeof natural14!=='boolean'||typeof firstFire!=='boolean'||typeof firstWater!=='boolean'||Number(stockout)+Number(natural14)+Number(firstFire)+Number(firstWater)>1)throw Error('Paired full launch profile must be explicit');
 if(natural14&&(!Number.isSafeInteger(mode14SourceTick)||!Number.isSafeInteger(lastSelectedSourceTick)||mode14SourceTick<0||mode14SourceTick>0xffffffff||lastSelectedSourceTick>0xffffffff||lastSelectedSourceTick<=mode14SourceTick))throw Error('Natural14 arm coordinates invalid');
 if((firstFire||firstWater)&&(![requestSourceTick,callbackSourceTick,lastSelectedSourceTick].every(x=>Number.isSafeInteger(x)&&x>=0&&x<=0xffffffff)||!(requestSourceTick<callbackSourceTick&&callbackSourceTick<lastSelectedSourceTick)))throw Error('First-fire arm coordinates invalid');
 const m=globalThis.Module;
 const arm=firstWater?'_melee_web_native_menu_stadium_paired_v11_first_water_pair_arm':firstFire?'_melee_web_native_menu_stadium_paired_v11_first_fire_pair_arm':natural14?'_melee_web_native_menu_stadium_paired_v11_natural_mode14_pair_arm':stockout?'_melee_web_native_menu_stadium_paired_v11_stockout_pair_arm':'_melee_web_native_menu_stadium_paired_v11_full_pair_arm';
 if(!m||typeof m._melee_web_native_menu_launch!=='function'||typeof m[arm]!=='function'||m.__pairedFullLaunch)throw Error('Paired full launch interface unavailable/already wrapped');
 const original=m._melee_web_native_menu_launch;let used=false;
 m.__pairedFullLaunch={original,called:false};
 m._melee_web_native_menu_launch=function(...args){
  if(used)throw Error('Paired full launch is one-use');used=true;
  if(m.stadiumMatchedSetupNativeIdle?.()!==true)throw Error('Paired full launch owner is not settled');
  if(((firstFire||firstWater)?m[arm](requestSourceTick,callbackSourceTick,lastSelectedSourceTick):natural14?m[arm](mode14SourceTick,lastSelectedSourceTick):m[arm]())!==1)throw Error('Paired full native arm refused: '+m.UTF8ToString(m._melee_web_native_menu_message()));
  m.__pairedFullLaunch.called=true;return original.apply(this,args);
 };
 return true;
}
export class PairedFullComparator{
 constructor(expected){this.e=expected;this.order=0;this.action=0;this.current=null;this.probes=new Map();this.matches=expected.matches.map(()=>({generation:null,setup:false,go:false,state:0,window:false,terminal:false,retired:false,results:false,css:false,endpoint:0,actions:0,selectedEvents:0,unpairedNativeEvents:0}));this.complete=false;}
 acceptProbe(text){const p=JSON.parse(text);validateRngDrawProbeRows([text],{selected:[p.source_cursor]});need(p.version===3&&!this.probes.has(p.source_cursor),'duplicate/unsupported raw probe');this.probes.set(p.source_cursor,p);}
 accept(row){
  need(row?.record==='stadium_paired_v11'&&row.schema==='melee-web-stadium-paired-v11-row-v1'&&row.order===this.order++,'stream schema/order');
  need(row.match_index===0||row.match_index===1,'match index');const i=row.match_index,m=this.matches[i],e=this.e.matches[i],r=e.reference,p=row.payload;
  need(i===0||this.matches[0].css,'B preceded A Results/CSS');
  need(Number.isSafeInteger(row.world_generation)&&row.world_generation>0,'live generation');
  if(m.generation===null){need(i===0||row.world_generation!==this.matches[0].generation,'reused A world generation');m.generation=row.world_generation;}
  need(row.world_generation===m.generation&&row.diagnostic_action_index===this.action,'owner/action generation changed');
  if(row.kind==='setup'){
   need(!m.setup&&p.schema==='melee-web-stadium-paired-v11-setup-v1'&&p.match_index===i&&p.world_generation===m.generation&&p.world_ticks===0&&p.source_frame===0&&p.input_cursor===r.actions[0].frame_index&&p.seed_hex===r.setup.rng.value_hex,'zero-tick Setup seed/owner');
   if(!(this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater)||i===1){
    const c=r.constructor.state_projection,w={scope:'shared-constructor-aggregate-only',entry_count:c.constructor_entry_count,parameter_roles:{min_parameter:'parameter_x0',max_parameter:'parameter_x4'},min_parameter:c.parameter_x0,max_parameter:c.parameter_x4,random_bound:c.random_bound,random_result:c.random_result,seed_before:c.seed_before_hex,seed_after:c.seed_after_hex,randi_call_count:1};
    need(same(p.constructor,w),'constructor seven aggregates');
   }m.setup=true;return;
  }
  if(row.kind==='scene_rng'){
   const n=m.endpoint,phase=['partitioned_entry_before','partitioned_entry_return','original_vs_exit_before','original_vs_exit_return'][n];
   need(n<4&&p.schema==='melee-web-stadium-paired-scene-rng-point-v1'&&p.phase===phase&&p.source_boundary_ordinal===n&&p.world_generation===m.generation&&p.original_callback_dispatch===(n>=2)&&seed(p.seed_hex)&&p.seed_hex===e.endpoints[n].value.toString(16).padStart(8,'0')&&/^0x[0-9a-f]{1,16}$/.test(p.seed_pointer_hex)&&BigInt(p.seed_pointer_hex)>0n&&BigInt(p.seed_pointer_hex)%4n===0n,'source-backed scene RNG endpoint');
   if(n>=2)need(m.terminal,'OnExit endpoint before terminal');
   if(m.seedPointer===undefined)m.seedPointer=p.seed_pointer_hex;
   need(m.seedPointer===p.seed_pointer_hex,'native seed owner pointer changed');m.endpoint++;return;
  }
  need(m.setup,'action before Setup');
  if(row.kind==='action_begin'){
   need(!this.current&&!m.retired&&m.actions<r.actions.length,'duplicate/unowned action');const a=r.actions[m.actions];
   need(row.local_action_index===m.actions&&this.action===e.offset+m.actions&&p.action===a.kind&&row.source_tick===a.source_tick&&row.draw_ordinal===a.draw_ordinal&&seed(p.seed_before_hex),'action begin coordinate');
   if(a.kind==='tick')need(row.input_cursor===a.frame_index,'literal input cursor');this.current={i,a,begin:p};return;
  }
  if(row.kind==='action_end'){
   need(this.current?.i===i,'action return without begin');const {a,begin}=this.current;
   need(p.action===a.kind&&row.local_action_index===m.actions&&row.source_tick===a.source_tick&&row.draw_ordinal===a.draw_ordinal&&seed(p.seed_after_hex),'action return coordinate');
   if(a.kind==='tick')need(p.input_cursor===a.frame_index,'completed input cursor');
   need(Number.isInteger(begin.source_frame_before)&&Number.isInteger(p.source_frame_after)&&p.source_frame_after>=begin.source_frame_before&&(a.kind==='tick'?p.source_frame_after-begin.source_frame_before<=1:p.source_frame_after===begin.source_frame_before),'source match clock transition');
   const probe=this.probes.get(this.action);need(probe,'missing actual RNG action row');
   const selected=a.selected_rng_event_ordinals,all=a.rng_event_ordinals;
   const full=!(this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater)&&a.after_sequence>=r.states[0].tick.sequence&&a.through_sequence<=r.window.target_source_tick.sequence;
   if(full){
    need(selected&&all&&selected[0]===all[0]&&selected[1]===all[1],'fully observed action RNG range');
    need(selected[1]-selected[0]===probe.draws.length,'selected action RNG count');m.selectedEvents+=probe.draws.length;
   }else m.unpairedNativeEvents+=probe.draws.length;
   this.probes.delete(this.action);this.action++;m.actions++;this.current=null;return;
  }
  need(!this.current,'observation before actual action return');
  if(row.kind==='go'){
   need(!(this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater)||i===1,'stockout unobserved A GO');
   need(!m.go&&compareLaterBGate2Go({reference:r,actualEvent:p,actualTick:p.world_ticks,admittedWorldGeneration:m.generation}).decision==='PASS_SHARED_GO_EVENT_FIELDS_AND_SEED','actual GO');m.go=true;return;
  }
  if(row.kind==='state'){
   need(!(this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater)||i===1,'stockout unobserved A state');
   need(m.go&&!m.window&&p.ordinal===m.state,'typed state ordinal');const s=r.states[m.state];need(s&&p.selected===s.selected,'selected/tail state scope');
   const typed=this.e.firstWater?firstWaterOriginalValues(s.state,s.tick.source_tick):this.e.firstFire?firstFireOriginalValues(s.state,s.tick.source_tick):typedOriginalValues(s.state,s.tick.source_tick),t=this.e.firstWater?p.first_water_state:this.e.firstFire?p.first_fire_state:p.typed22,q=p.pad_queue;
   for(const[k,v]of Object.entries(typed))need(Object.is(t?.[k],v),'typed22 '+k);
   need(t.schema===(this.e.firstWater?'melee-web-stadium-first-water-state-native-v1':this.e.firstFire?'melee-web-stadium-first-fire-state-native-v1':'melee-web-stadium-per-tick-common-fields-native-v3')&&t.world_generation===m.generation&&t.source_tick===s.tick.source_tick&&t.world_ticks===s.tick.source_tick+1&&t.seed_hex===s.rng.value_hex,'typed owner/seed');
   need(q.schema==='melee-web-stadium-hsd-pad-queue-observation'&&q.version===2&&q.source_generation===m.generation&&q.source_tick_before===s.tick.source_tick&&q.source_tick_after===s.tick.source_tick+1&&q.qnum_before===1&&q.qread_before===0&&q.qcount_before===1&&q.qcount_after_hsd===0&&q.tick_complete===true&&same(q.queue_slot_status_hex,s.pads),'literal four-port PAD queue');
   if(this.e.firstFire){validateFirstFireLocalGraph(p.first_fire_graph,m.generation);need(p.first_fire_graph.phase===t.map2_phase&&p.first_fire_graph.form===t.map2_form&&p.first_fire_graph.scalar_world_ticks===t.world_ticks,'first-fire live graph/scalar join');}
   if(this.e.firstWater){validateFirstWaterLocalGraph(p.first_water_graph,m.generation);need(p.first_water_graph.phase===t.map2_phase&&p.first_water_graph.form===t.map2_form&&p.first_water_graph.scalar_world_ticks===t.world_ticks,'first-water live graph/scalar join');}
   if(this.e.stockout){
    need(p.players?.length===2&&s.players?.length===2,'stockout two source players');
    for(let j=0;j<2;j++){
     need(p.players[j].fighter_present===true,'stockout original-present fighter missing');
     for(const key of ['player_index','player_slot','motion_id','ground_or_air','stocks'])need(Object.is(p.players[j][key],s.players[j][key]),'stockout player '+j+' '+key);
    }
   }m.state++;return;
  }
  if(row.kind==='window_closed'){
   need(!m.window&&m.state===r.states.length,'actual window closure order');
   if(this.e.firstWater){
    if(i===0)need(p.close_policy==='declared_match_actions_complete'&&m.actions===r.actions.length&&p.state_samples===0&&p.natural_tail_ticks===0,'first-water warmup close');
    else need(p.close_policy==='observed_first_water_settled'&&p.state_samples===r.states.length&&p.natural_tail_ticks===0&&p.last_selected_source_tick===this.e.projection.first_water.settled.source_tick,'first-water actual settled close');
   }else if(this.e.firstFire){
    if(i===0)need(p.close_policy==='declared_match_actions_complete'&&m.actions===r.actions.length&&p.state_samples===0&&p.natural_tail_ticks===0,'first-fire warmup close');
    else need(p.close_policy==='observed_first_fire_settled'&&p.state_samples===r.states.length&&p.natural_tail_ticks===0&&p.last_selected_source_tick===this.e.projection.first_fire.settled.source_tick,'first-fire actual settled close');
   }else if(this.e.natural14){
    if(i===0)need(p.close_policy==='declared_match_actions_complete'&&m.actions===r.actions.length&&p.state_samples===0&&p.natural_tail_ticks===0,'natural14 warmup closure');
    else need(p.close_policy==='observed_mode14_and_neutral_release'&&p.state_samples===r.states.length&&p.natural_tail_ticks===0&&p.mode14_source_tick===r.mode14_source_tick&&p.last_selected_source_tick===r.last_selected_source_tick&&p.mode14_observed===true&&p.neutral_release_observed===true,'natural14 actual mode/release closure');
   }else if(this.e.stockout)need(p.close_policy==='declared_match_actions_complete'&&m.actions===r.actions.length&&p.state_samples===r.states.length&&p.natural_tail_ticks===0&&p.source_ticks===r.actions.filter(a=>a.kind==='tick').length&&p.source_draws===r.actions.filter(a=>a.kind==='draw').length,'stockout declared action completion');
   else need(p.state_samples===3501&&p.natural_tail_ticks===r.window.natural_tail_ticks&&p.close_source_tick===r.window.close.source_tick&&p.draw_ordinal===r.window.close.draw_ordinal,'actual natural window closure');
   m.window=true;return;
  }
  if(row.kind==='tail_before'){need((this.e.stockout?i===0:(this.e.natural14||this.e.firstFire||this.e.firstWater)?i===0||m.window:m.window)&&!m.retired&&Number.isInteger(p.pause_timer),'actual pause tail');return;}
  if(row.kind==='terminal'){need(m.window&&!m.terminal&&m.actions===r.actions.length,'natural Match terminal action extent');m.terminal=true;return;}
  if(row.kind==='published_result'){
   need(this.e.stockout&&m.terminal&&m.endpoint===4&&!m.published&&!m.retired,'stockout published result order');
   need(p.outcome===(i===0?7:2),'stockout published outcome');
   if(i===1)need(p.winner_count===1&&same(p.winners,[1]),'stockout published winner inferred from source final stocks');
   m.published=true;return;
  }
  if(row.kind==='retired'){if(this.e.firstFire&&i===1){validateFirstFireLocalGraph(p.first_fire_graph,m.generation,true);validateFirstFireCompletion(p.first_fire_completion,this.e.projection.first_fire);}if(this.e.firstWater&&i===1){validateFirstWaterLocalGraph(p.first_water_graph,m.generation,true);validateFirstWaterCompletion(p.first_water_completion,this.e.projection.first_water);}need(m.terminal&&!m.retired&&m.endpoint===4&&(!this.e.stockout||m.published)&&p.native_world_absent===true&&p.parent_seed_restored===true&&seed(p.handoff_seed_hex)&&seed(p.restored_parent_seed_hex),'checked natural world retirement');m.retired=true;return;}
  if(row.kind==='results_request'){need(m.retired&&!m.resultsRequested&&p.typed_results_requested===true,'Results request');m.resultsRequested=true;return;}
  if(row.kind==='results'){need(m.resultsRequested&&!m.results&&p.typed_results_entered===true&&Number.isSafeInteger(p.world_generation)&&p.world_generation!==m.generation&&seed(p.entry_seed_hex)&&seed(p.seed_hex),'actual Results owner');m.results=true;return;}
  if(row.kind==='css'){need(m.results&&!m.css&&p.original_css_entered===true,'actual CSS return');m.css=true;return;}
  if(row.kind==='complete'){need(i===1&&this.matches.every(x=>x.css)&&this.action===this.e.total&&p.whole_session_input_complete===true&&p.C3===false,'actual complete lifetime');this.complete=true;return;}
  throw Error('Paired full comparison: unsupported row kind '+row.kind);
 }
 finish(status){need(this.complete&&!this.current&&this.probes.size===0&&status?.schema==='melee-web-stadium-paired-v11-full-pair-v1'&&status.full_pair===true&&status.whole_session_complete===true&&status.failed===false&&status.diagnostic_actions===this.e.total&&status.lifetimes?.length===2,'complete native trace');this.matches.forEach((m,i)=>{
 if(this.e.firstWater)need(status.profile==='selected_b_first_water_recorded_callback_v1'&&status.per_owner_rng_comparison===false,'first-water final profile');
 else if(this.e.firstFire)need(status.profile==='selected_b_first_fire_recorded_callback_v1'&&status.per_owner_rng_comparison===false,'first-fire final profile');
 else if(this.e.natural14)need(status.profile==='selected_b_natural_mode14_v1'&&status.per_owner_rng_comparison===false,'natural14 final profile/outcome');
 else if(this.e.stockout)need(status.profile==='selected_b_natural_stockout_v1'&&status.per_owner_rng_comparison===false&&m.published,'stockout final profile/published outcome');
 else need(m.selectedEvents===this.e.matches[i].reference.selected_rng_events,'selected RNG coverage');const l=status.lifetimes[i],r=this.e.matches[i].reference;
 if(this.e.natural14)need(i===0||l.mode14_observed===true&&l.neutral_release_observed===true,'natural14 native final outcome counters');
 if(this.e.stockout)need(l.published_result===true&&(i===0||l.stock_losses===4&&l.mode9_observed===true),'stockout native final outcome counters');
 need(l.match_index===i&&l.world_generation===m.generation&&l.actions===m.actions&&l.ticks===r.actions.filter(a=>a.kind==='tick').length&&l.draws===r.actions.filter(a=>a.kind==='draw').length&&l.state_samples===((this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater)?r.states.length:3501)&&l.natural_tail_ticks===r.window.natural_tail_ticks&&l.window_closed===true&&l.retired===true&&l.results_requested===true&&l.results_entered===true&&l.css_returned===true&&l.scene_rng_count===4,'final native lifetime counters/owners');
 });return {pass:true,actions:this.action,lifetimes:this.matches.map((m,i)=>({match_index:i,...m})),RNG_semantics_requires_separate_python_comparison:!(this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater),...((this.e.stockout||this.e.natural14||this.e.firstFire||this.e.firstWater)?{per_owner_rng_comparison:false}:{}),entry_dispatch_parity:false,raw_pointer_equality:false,C3:false};}
}
