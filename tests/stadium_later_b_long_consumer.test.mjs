import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';
import {deriveLaterBLongExpectations,inspectLaterBLongReferenceFields,encodeLaterBLongNativeManifest,StadiumSetupCfActionDriver} from './stadium_matched_setup_cf_consumer.mjs';
import {deriveLongBActionRngWindow,bindLongBActionRngRows,activateLongBActionRngSelection} from '../scripts/stadium_later_b_long_rng.mjs';
const names=['MELEE_LONG_B_PROJECTION','MELEE_LONG_B_BINDING'];
const available=names.every(n=>process.env[n]);
if(!available&&names.some(n=>process.env[n]))throw Error('Partial long B fixture configuration');
const retained={skip:available?false:'retained long B fixture not configured'};
const raw=available?fs.readFileSync(process.env[names[0]]):null;
const binding=available?fs.readFileSync(process.env[names[1]]):null;
const source=available?deriveLaterBLongExpectations(raw,binding):null;
const UUID='12345678-1234-4234-8234-123456789abc';
const generation=11;
// Native receipt values are MODELED controls against retained source operands.
// They test admission and failure behavior, never browser/native parity.
function setup(){const c=source.reference.constructor_shared_projection;return {
  handoff_origin:'current_sss_prepared_later_951',complete:true,captured:true,source_frames:0,host_tick_calls:0,host_draw_calls:0,
  actual:{random_seed_hex:source.reference.setup.rng.value_hex},stadium_setup_state:{
    schema:'melee-web-stadium-zero-tick-setup-state-v1',world_generation:generation,world_ticks:0,source_frame:0,map_id:2,
    source_parameter_roots_agree:true,constructor_callback_count:c.entry_count,parameter_x0:c.min_parameter,parameter_x4:c.max_parameter,
    constructor_randi_bound:c.random_bound,constructor_randi_result:c.random_result,source_seed_before_hex:c.seed_before,
    source_seed_after_hex:c.seed_after,constructor_randi_call_count:1,constructor_randi_scope:'existing_inline_grStadium_randi_call'}};}
function fixture(){const approvals=[];const driver=new StadiumSetupCfActionDriver({laterBLong:true,laterBProjection:raw,
  laterBProjectionBinding:binding,laterBToken:UUID,admittedWorldGeneration:generation,native:{arm:async()=>1,queue:async()=>1,
    approve:async(i,pass)=>{approvals.push([i,pass]);return 1;}}});return {driver,approvals};}
function receipt(driver,index){
  const a=source.runtimePayload.schedule[index],c=source.actionCounts[index],tick=a.action==='tick',
    ticks=c.ticks+Number(tick),draws=c.draws+Number(!tick),before=driver.sourceFrame,
    after=before+Number(tick&&a.bundle>=84),expected=source.commonRowsByAction.get(index),
    ctor=source.reference.constructor_shared_projection,{scope,...aggregate}=ctor;
  return {schema:'melee-web-stadium-later-b-long-native-v1',scope:'observed-b-7168-actions-go-event-and-post-go-typed22-observation-only',
    reference_id:source.reference.reference_id,recipe_sha256:source.reference.recipe_sha256,
    action_count:7168,cursor:index,prefix_cursor:951,reference_go_tick:84,stop_tick:3585,source_ticks:ticks,source_draws:draws,
    post_go_typed22_state_count:Math.max(0,ticks-84),actual_go:{observed:ticks>84,tick:ticks>84?84:null},
    constructor_shared_projection:{scope:'shared-constructor-aggregate-only',...aggregate,randi_call_count:1},
    last_action:{complete:true,schedule_index:index,action:a.action,ordinal:tick?a.bundle:a.draw,...(tick?{bundle:a.bundle}:{}),
      source_ticks_before:c.ticks,source_ticks_after:ticks,source_draws_before:c.draws,source_draws_after:draws,
      world_ticks:ticks,world_generation:generation,source_frames_before:before,source_frames_after:after,source_frames:after,
      source_tick_input_pad_hex:tick?source.runtimePayload.bundles[a.bundle].ports.map(p=>p.status_hex).join(''):null,
      source_coordinates:{source_tick:tick?a.bundle:c.ticks,draw_ordinal:tick?null:a.draw,recipe_input_index:tick?951+a.bundle:null},
      rng_action_scope:'diagnostic-action-index',rng_action_index:index>=168?index:null,
      actual_go_event:index===168?{generation,world_ticks:84,sequence:3,source_branch:0,
        map2_gate:Number(source.reference.go.state.map2.gate_mask_80),display_mode:source.reference.go.state.display.mode_s16,
        source_seed_valid:1,source_seed_hex:source.reference.go.rng.value_hex}:null,
      post_go_typed22_state:expected?{schema:'melee-web-stadium-per-tick-common-fields-native-v3',schedule_index:index,
        source_tick:a.bundle,world_ticks:ticks,source_frame:after,world_generation:generation,...expected.values,seed_hex:expected.seed_hex}:null}};
}
test('long mode refuses missing exact serialized operands',()=>{
  assert.throws(()=>deriveLaterBLongExpectations({},{}));
  assert.throws(()=>new StadiumSetupCfActionDriver({laterBLong:true}));
});
test('actual variable schedule and all literal pads survive MWB3 encoding',retained,()=>{
  assert.equal(source.runtime_admission,false);assert.equal(source.commonRowsByAction.size,3501);
  const wire=encodeLaterBLongNativeManifest(source.runtimePayload,UUID),view=new DataView(wire.buffer);
  assert.equal(Buffer.from(wire.subarray(0,4)).toString(),'MWB3');
  assert.deepEqual([4,8,12,16,20].map(i=>view.getUint32(i,true)),[7168,951,84,3585,36]);
  for(const [i,a] of source.sourceActions.entries()){
    const offset=60+i*57,c=source.actionCounts[i],tick=a.kind==='tick';
    assert.deepEqual([1,5,9].map(j=>view.getUint32(offset+j,true)),[tick?c.ticks:c.draws,c.ticks,c.draws]);
    assert.equal(Buffer.from(wire.subarray(offset+13,offset+57)).toString('hex'),tick?a.pads.join(''):'00'.repeat(44));
  }
  assert.deepEqual(source.sourceActions.filter((a,i)=>i&&a.kind==='tick'&&source.sourceActions[i-1].kind==='tick').map(a=>a.action_index),[4929,6930]);
});
test('raw mutation, wrong source order, state owner and natural terminal refuse',retained,()=>{
  const bytes=Buffer.from(raw);bytes[bytes.length-2]^=1;assert.throws(()=>deriveLaterBLongExpectations(bytes,binding));
  for(const change of [p=>p.actions[4929].draws_before++,p=>p.states[10].rng.value_address+=4,
    p=>p.go.coordinate.sequence=p.actions[168].source.sequence,p=>p.natural_terminal.natural_tail_ticks++,p=>p.C3=true]){
    const p=structuredClone(source.reference);change(p);assert.throws(()=>inspectLaterBLongReferenceFields(p));
  }
});
test('modeled native receipts complete exact long schedule and preserve empty action RNG rows',retained,async()=>{
  const f=fixture(),receipts=new Map(),rng=[];assert.equal(await f.driver.arm(true,setup()),true);
  for(let i=0;i<7168;i++){
    assert.equal(await f.driver.submit(source.runtimePayload.schedule[i]),true);
    const r=receipt(f.driver,i);assert.equal(await f.driver.accept(r),true,`${i}: ${f.driver.status().first_error}`);
    if(i>=168){receipts.set(i,r);rng.push(JSON.stringify({schema:'melee-web-rng-draw-probe',version:3,source_cursor:i,overflowed:false,draws:[]}));}
  }
  assert.equal(f.driver.status().complete,true);assert.equal(f.approvals.length,7168);
  assert.equal(f.driver.lastCommonComparison.exact_value_count,23);
  const window=deriveLongBActionRngWindow('actions',source),bound=bindLongBActionRngRows(rng,window,receipts);
  assert.equal(bound.complete,true);assert.equal(bound.rows.length,7000);
  assert.deepEqual(bound.rows[4929-168].source_coordinate,source.reference.action_rng.rows[4929-168].source_coordinate);
  receipts.get(4929).last_action.source_draws_before++;
  assert.throws(()=>bindLongBActionRngRows(rng,window,receipts),/exact completed native action/);
});
test('wrong actual state stops approval at the first mismatching action',retained,async()=>{
  const f=fixture();assert.equal(await f.driver.arm(true,setup()),true);
  for(let i=0;i<=168;i++){
    assert.equal(await f.driver.submit(source.runtimePayload.schedule[i]),true);const r=receipt(f.driver,i);
    if(i===168)r.last_action.post_go_typed22_state.seed_hex='00000000';
    assert.equal(await f.driver.accept(r),i!==168);
  }
  assert.equal(f.approvals.filter(([,pass])=>pass).length,168);assert.equal(f.driver.status().complete,false);
});

test('serialized action activation is one-use after an empty menu selection',()=>{
 const configuration={schema:'melee-web-later-b-long-action-rng-v1',reference_id:'original-b-5422-951-long-v1',
  prefix_cursor:951,reference_go_tick:84,first_action:168,last_action:7167};
 const window={__meleeLaterBRngOwnerWindow:configuration,__meleeRngDrawProbeOwners:true,__meleeRngDrawProbeFighterOwners:true,
  __meleeRngDrawProbeCursors:[],__meleeRngDrawProbeRange:null,__rngDrawProbeRows:[]};
 const activate=vm.runInNewContext('('+activateLongBActionRngSelection.toString()+')',{window});
 const args={configuration,selected:Array.from({length:7000},(_,i)=>168+i)};
 assert.equal(activate(args).activated,true);assert.equal(window.__meleeRngDrawProbeCursors.length,7000);
 assert.throws(()=>activate(args),/empty pre-arm/);
 window.__meleeRngDrawProbeCursors=[];window.__rngDrawProbeRows=['unexpected'];
 assert.throws(()=>activate(args),/empty pre-arm/);
});
