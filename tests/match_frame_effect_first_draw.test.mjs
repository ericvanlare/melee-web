import test from 'node:test';
import assert from 'node:assert/strict';
import {schema2Fixture} from './match_frame_effect_schema2_fixture.mjs';
import {frameEffectObservationFailures} from './match_frame_effect_driver.mjs';
import {firstDrawObservationFailures,compareFirstDrawObservations,armNativeFrameEffectFirstDraw,
        readNativeFrameEffectFirstDraw} from './match_frame_effect_first_draw_driver.mjs';
function fixture(total=2){
 const r=schema2Fixture();r.schema_version=3;r.scope='first_draw_in_normal_callback_observation_only';
 r.planned_steps=r.callback_steps=r.callback_draws=r.tick_returns=r.draw_returns=total;
 r.source_frame_after=r.source_frame_before+total;r.selected_source_frame=r.source_frame_before+1;
 r.unobserved_draws=total-1;r.selected_depth_owners={registered:0,completed:0,pending:0,ignored_other_issuer:3,finalized:false};
 return r;
}
test('explicit first draw retains exact renderer rows and total callback counts',()=>{
 for(const n of [1,2,8]){const r=fixture(n);assert.deepEqual(firstDrawObservationFailures(r),[]);
  assert.deepEqual(compareFirstDrawObservations(r,structuredClone(r)),[]);assert.deepEqual(frameEffectObservationFailures(r),['wrong_scope_or_schema']);}
 assert.deepEqual(firstDrawObservationFailures(schema2Fixture()),['wrong_scope_or_schema']);
});
test('missing, unknown, order and value differences fail exactly',()=>{
 const r=fixture();
 for(const change of [x=>{delete x.unobserved_draws;},x=>{x.extra=true;},x=>{x.selected_source_frame++;},
   x=>{x.callback_draws=1;},x=>{x.unobserved_draws=0;},x=>{x.selected_depth_owners.pending=1;},
   x=>{x.selected_depth_owners.completed=1;},x=>{x.selected_depth_owners.extra=0;},
   x=>{x.draws[0].viewport_bits[0]=0;},x=>{x.events.reverse();}]){
  const c=structuredClone(r);change(c);assert.notDeepEqual(c,r,'corruption control must change the receipt');
  assert.ok(compareFirstDrawObservations(r,c).length);
 }
 const c=structuredClone(r);c.selected_depth_owners.ignored_other_issuer++;
 assert.deepEqual(compareFirstDrawObservations(r,c),['different:receipt.selected_depth_owners.ignored_other_issuer']);
});
test('first draw entry and pending map reads use actual JS idle guard',()=>{
 const original=globalThis.Module;let nativeCalls=0;
 try{
  globalThis.Module={matchFrameEffectNativeIdle:()=>false,
    _melee_web_native_menu_match_observe:()=>{nativeCalls++;},
    _melee_web_native_menu_frame_effect_status:()=>{nativeCalls++;}};
  assert.equal(armNativeFrameEffectFirstDraw().reason,'native_call_pending');
  assert.deepEqual(readNativeFrameEffectFirstDraw(),{settled:false});assert.equal(nativeCalls,0);
  let value=fixture();value.terminal=true;value.renderer.fence_count=1;
  globalThis.Module={matchFrameEffectNativeIdle:()=>true,matchFrameEffectCallbackReturned:true,
    UTF8ToString:x=>x,_melee_web_native_menu_frame_effect_status:()=>JSON.stringify(value)};
  assert.equal(readNativeFrameEffectFirstDraw().completed,true);
  value.selected_depth_owners.pending=1;assert.equal(readNativeFrameEffectFirstDraw().completed,false);
  value.error='failed';assert.equal(readNativeFrameEffectFirstDraw().completed,false);
 }finally{globalThis.Module=original;}
});

test('selected map callbacks retain issuing owner before or after selected fence',()=>{
 const r=fixture();const row=(flags,extra={})=>({kind:22,frame_index:3,op_index:0,item_index:0,flags,
   value0:0,value1:0,value2:0,value3:0,handle_a:'0',handle_b:'0',...extra});
 const registered=row(0x205,{value0:8,value2:1,handle_a:'3',handle_b:'1'});
 const complete=row(6,{value0:1,value1:1,handle_a:'4294967297',handle_b:'1'});
 r.events.splice(r.events.length-1,0,registered);r.events.push(complete);
 r.renderer.event_count=r.events.length;r.renderer.depth_trace_count=2;
 r.selected_depth_owners.registered=r.selected_depth_owners.completed=1;
 assert.deepEqual(firstDrawObservationFailures(r),[]);
 for(const mutate of [x=>{x.events.at(-1).handle_b='2';},x=>{x.events.at(-1).value0=2;},
   x=>{x.events.at(-1).value1=0;},x=>{x.selected_depth_owners.registered=0;},
   x=>{x.events.at(-1).frame_index=4;},x=>{x.events.at(-2).flags=0x205;}]){
  const c=structuredClone(r);mutate(c);assert.ok(firstDrawObservationFailures(c).length);
 }
});
