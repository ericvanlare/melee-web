import assert from 'node:assert/strict';
import fs from 'node:fs';
import {pathToFileURL} from 'node:url';
import test from 'node:test';
import * as consumer from './stadium_matched_setup_cf_consumer.mjs';

// Native leaves below are MODELED component controls, not game/lifetime proof.
const names=['MELEE_LATER_B_GATE2_PROJECTION','MELEE_LATER_B_GATE2_SETUP','MELEE_LATER_B_GATE2_BASE_HELPER'];
const configured=names.filter(name=>process.env[name]);
if(configured.length && configured.length!==names.length)
  throw Error('Partial explicit Gate2 consumer fixture configuration');
const available=configured.length===names.length;
const raw=available?fs.readFileSync(process.env[names[0]]):null;
const reference=available?JSON.parse(raw.toString('utf8')):null;
const actualSetup=available?JSON.parse(fs.readFileSync(process.env[names[1]],'utf8')).prepared_to_setup.setup:null;
const base=available?await import(pathToFileURL(process.env[names[2]])):null;
const retained={skip:available?false:'retained Gate2 reference/Setup/base fixture set wholly absent'};
const UUID='12345678-1234-4234-8234-123456789abc';
const clone=value=>structuredClone(value);
function sources(){return consumer.deriveLaterBGate2Expectations(raw);}
function driverFixture() {
  const source=sources(),calls=[];
  const native={arm:async bytes=>{calls.push(['arm',bytes]);return 1;},
    queue:async(index,action,bytes)=>{calls.push(['queue',index,action,bytes]);return 1;},
    approve:async(index,pass)=>{calls.push(['approve',index,pass]);return 1;}};
  const driver=new consumer.StadiumSetupCfActionDriver({laterBProjection:raw,laterBToken:UUID,native,
    admittedWorldGeneration:actualSetup.stadium_setup_state.world_generation});
  return{source,calls,driver,native};
}
function goEvent(f) {
  return {generation:f.driver.admittedWorldGeneration,world_ticks:f.source.goTick,source_frame:0,
    sequence:3,callback_index:41,source_branch:0,map2_gate:Number(reference.go.state.map2.gate_mask_80),
    display_mode:reference.go.state.display.mode_s16,remap_branch:0,callback_identity:1,hud_enabled:0,
    source_seed_valid:1,source_seed_hex:reference.go.rng.value_hex};
}
function receipt(f,index) {
  const action=f.driver.payload.schedule[index],tick=Math.floor(index/2),isTick=action.action==='tick';
  const ticksBefore=Math.floor((index+1)/2),ticksAfter=tick+1,drawsBefore=Math.floor(index/2),drawsAfter=Math.floor((index+1)/2);
  // Explicit modeled match clock. No claim this fixture is an original clock trace.
  const before=f.driver.sourceFrame,after=isTick&&tick>=f.source.goTick?before+1:before;
  const selected=isTick&&tick>=f.source.goTick;
  const expected=f.source.commonRowsByAction.get(index);
  const sample=selected?{schema:'melee-web-stadium-per-tick-common-fields-native-v3',schedule_index:index,
    source_tick:tick,world_ticks:ticksAfter,source_frame:after,world_generation:f.driver.admittedWorldGeneration,
    ...expected.values,seed_hex:expected.seed_hex}:null;
  const {scope,...constructor}=reference.constructor_shared_projection;
  return {schema:'melee-web-stadium-later-b-gate2-native-v1',
    scope:'observed-b-first180-actions-go-event-and-post-go-typed22-observation-only',
    action_count:360,cursor:index,source_ticks:ticksAfter,source_draws:drawsAfter,prefix_cursor:943,
    reference_go_tick:f.source.goTick,stop_tick:180,
    actual_go:{observed:ticksAfter>f.source.goTick,tick:ticksAfter>f.source.goTick?f.source.goTick:null},
    post_go_typed22_state_count:Math.max(0,ticksAfter-f.source.goTick),
    constructor_shared_projection:{scope:'shared-constructor-aggregate-only',...constructor,randi_call_count:1},
    last_action:{complete:true,schedule_index:index,action:action.action,ordinal:tick,...(isTick?{bundle:tick}:{}),
      source_ticks_before:ticksBefore,source_ticks_after:ticksAfter,source_draws_before:drawsBefore,source_draws_after:drawsAfter,
      world_ticks:ticksAfter,world_generation:f.driver.admittedWorldGeneration,
      source_frames_before:before,source_frames_after:after,source_frames:after,
      source_tick_input_pad_hex:isTick?f.driver.payload.bundles[tick].ports.map(p=>p.status_hex).join(''):null,
      source_coordinates:{source_tick:isTick?tick:tick+1,draw_ordinal:isTick?null:tick,recipe_input_index:isTick?943+tick:null},
      actual_go_event:isTick&&tick===f.source.goTick?goEvent(f):null,post_go_typed22_state:sample}};
}
async function runTo(f,index) {
  assert.equal(await f.driver.arm(true,actualSetup),true);
  for(let i=0;i<index;i++) {
    assert.equal(await f.driver.submit(f.driver.payload.schedule[i]),true);
    assert.equal(await f.driver.accept(receipt(f,i)),true,`${i}: ${f.driver.status().first_error}`);
  }
}
test('new runtime scope rejects mutable objects and cannot impersonate legacy admission',()=>{
  assert.throws(()=>consumer.deriveLaterBGate2Expectations({}),/serialized projection/);
  assert.throws(()=>new consumer.StadiumSetupCfActionDriver({laterBProjection:{},laterBToken:UUID}),/serialized projection/);
  assert.throws(()=>new consumer.StadiumSetupCfActionDriver({laterBToken:UUID}),/explicit serialized projection/);
});
test('actual retained projection joins all raw actions, phase identities and MWB2 bytes',retained,()=>{
  const s=sources(),bytes=consumer.encodeLaterBGate2NativeManifest(s.runtimePayload,UUID);
  assert.equal(s.runtime_admission,false);assert.equal(s.runtimePayload.schedule.length,360);
  assert.equal(s.commonRowsByAction.size,96);assert.equal(s.runtimePayload.schedule[0].action,'tick');
  assert.equal(s.reference.go.coordinate.sequence,4753);assert.notEqual(s.reference.go.coordinate.sequence,s.reference.states[0].tick.sequence);
  assert.equal(s.reference.entry.seed,'ba5dac05');assert.equal(s.reference.setup.seed,'4b25c79b');
  assert.equal(bytes.length,20580);assert.equal(Buffer.from(bytes.subarray(0,4)).toString(),'MWB2');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  assert.deepEqual([4,8,12,16,20].map(i=>view.getUint32(i,true)),[360,943,s.goTick,180,36]);
  assert.equal(Buffer.from(bytes.subarray(24,60)).toString(),UUID);
  for(let tick=0;tick<180;tick++) {
    const a=60+tick*114,d=a+57;
    assert.equal(bytes[a],1);assert.equal(bytes[d],2);
    assert.deepEqual([1,5,9].map(i=>view.getUint32(a+i,true)),[tick,tick,tick]);
    assert.deepEqual([1,5,9].map(i=>view.getUint32(d+i,true)),[tick,0xffffffff,tick]);
    assert.equal(Buffer.from(bytes.subarray(a+13,a+57)).toString('hex'),reference.actions[tick*2].pads.join(''));
    assert.ok(bytes.subarray(d+13,d+57).every(x=>x===0));
  }
  assert.throws(()=>consumer.encodeLaterBGate2NativeManifest(s.runtimePayload,'not-a-uuid'),/UUID/);
});
test('hash admission plus actual semantic predicates refuse mutated phases/order/PAD/owners',retained,()=>{
  const badRaw=Buffer.from(raw);badRaw[43]^=1;assert.throws(()=>consumer.deriveLaterBGate2Expectations(badRaw),/identity differs/);
  const cases=[p=>{p.actions[0].pads[0]='ff';},p=>{p.actions[2].consume.sequence=p.actions[1].draw_return.sequence;},
    p=>{p.actions[0].kind='draw';},p=>{p.go.coordinate.sequence=p.actions[p.go.coordinate.source_tick*2].tick.sequence;},
    p=>{p.go.coordinate.source_tick--;},p=>{p.states[0].rng.value_address+=4;},p=>{p.states.reverse();},p=>{p.setup.seed=p.entry.seed;},
    p=>{p.constructor_original_witnesses.coordinates.reverse();},p=>{p.stop_before_consume.source_tick--;},
    p=>{p.source_spans[6].first_frame=940;},p=>{p.runtime_admission=true;}];
  for(const change of cases){const p=clone(reference);change(p);assert.throws(()=>consumer.inspectLaterBGate2ReferenceFields(p));}
  const p=clone(reference);p.actions[0].pads[0]='01'+p.actions[0].pads[0].slice(2);
  assert.throws(()=>consumer.deriveLaterBGate2Expectations(JSON.stringify(p)),/identity differs/);
});
test('seven actual Setup aggregate mappings and separate seed/generation refuse before native arm',retained,async()=>{
  for(const change of [s=>{s.stadium_setup_state.constructor_callback_count++;},s=>{s.stadium_setup_state.parameter_x0++;},
    s=>{s.stadium_setup_state.parameter_x4++;},s=>{s.stadium_setup_state.constructor_randi_bound++;},
    s=>{s.stadium_setup_state.constructor_randi_result++;},s=>{s.stadium_setup_state.source_seed_before_hex='00000000';},
    s=>{s.stadium_setup_state.source_seed_after_hex='00000000';},s=>{s.stadium_setup_state.constructor_randi_call_count=0;},
    s=>{s.actual.random_seed_hex='00000000';},s=>{s.stadium_setup_state.world_generation++;},s=>{s.host_tick_calls++;}]) {
    const f=driverFixture(),setup=clone(actualSetup);change(setup);
    assert.equal(await f.driver.arm(true,setup),false);assert.equal(f.calls.length,0);
  }
  const f=driverFixture();assert.equal(await f.driver.arm(true,actualSetup),true);
  assert.equal(f.driver.constructorComparison.compared_fields.length,7);
  assert.equal(f.driver.constructorComparison.setup_seed.actual,'4b25c79b');
  assert.equal(await f.driver.arm(true,actualSetup),false);
});
test('existing one-use driver completes360 modeled native actions against actual96 typed/seed expectations',retained,async()=>{
  const f=driverFixture();await runTo(f,360);
  assert.equal(f.driver.status().complete,true);assert.equal(f.calls.filter(x=>x[0]==='queue').length,360);
  assert.equal(f.calls.filter(x=>x[0]==='approve'&&x[2]===true).length,360);
  assert.equal(f.driver.goComparison.compared_fields.length,3);
  assert.equal(f.driver.lastCommonComparison.exact_value_count,23);
  assert.equal(await f.driver.submit(f.driver.payload.schedule[0]),false);
});
test('actual GO is mandatory, placed once, and never replaced by first post-tick state',retained,async()=>{
  for(const change of [r=>{r.last_action.actual_go_event=null;},r=>{r.actual_go.observed=false;},
    r=>{r.actual_go.tick++;},r=>{r.last_action.actual_go_event.source_seed_hex='00000000';},r=>{r.last_action.actual_go_event.generation++;},
    r=>{r.last_action.actual_go_event.world_ticks++;}]) {
    const f=driverFixture();await runTo(f,f.source.goActionIndex);
    assert.equal(await f.driver.submit(f.driver.payload.schedule[f.driver.cursor]),true);
    const r=receipt(f,f.driver.cursor);change(r);assert.equal(await f.driver.accept(r),false);
  }
  const f=driverFixture();await runTo(f,1);assert.equal(await f.driver.submit(f.driver.payload.schedule[1]),true);
  const r=receipt(f,1);r.last_action.actual_go_event=goEvent(f);assert.equal(await f.driver.accept(r),false);
});
test('wrong input/raw state/seed/generation, comparison forgery and caller mutation cannot gain approval',retained,async()=>{
  for(const index of [0,1]){
    const f=driverFixture();await runTo(f,index);
    assert.equal(await f.driver.submit(f.driver.payload.schedule[index]),true);
    const approved=f.calls.filter(row=>row[0]==='approve'&&row[2]===true).length;
    const r=receipt(f,index);r.last_action.world_generation++;
    assert.equal(await f.driver.accept(r),false);
    assert.equal(f.calls.filter(row=>row[0]==='approve'&&row[2]===true).length,approved);
    assert.match(f.driver.status().first_error,/owner/);
  }
  for(const change of [r=>{r.last_action.source_tick_input_pad_hex='00'.repeat(44);},
    r=>{r.last_action.post_go_typed22_state.seed_hex='00000000';},
    r=>{r.last_action.post_go_typed22_state.map2_countdown++;},
    r=>{r.last_action.post_go_typed22_state.world_generation++;},
    r=>{r.last_action.source_coordinates.recipe_input_index=940;},r=>{r.last_action.post_go_typed22_state=null;}]) {
    const f=driverFixture();await runTo(f,f.source.goActionIndex);
    assert.equal(await f.driver.submit(f.driver.payload.schedule[f.driver.cursor]),true);
    const r=receipt(f,f.driver.cursor);change(r);assert.equal(await f.driver.accept(r),false);
  }
  const f=driverFixture();f.driver.nativeManifest[60+13]^=1;assert.equal(await f.driver.arm(true,actualSetup),false);assert.equal(f.calls.length,0);
  const g=driverFixture();g.driver.commonRowsByAction.clear();await runTo(g,g.source.goActionIndex);
  assert.equal(await g.driver.submit(g.driver.payload.schedule[g.driver.cursor]),true);
  assert.equal(await g.driver.accept(receipt(g,g.driver.cursor),null,{decision:'PASS_EXACT_22_COMMON_FIELDS_AND_SEED'}),false);
});
test('legacy MWP2/MWF3/MWC5/MWC6 encoding stays byte identical and rejects new mode cross-use',retained,()=>{
  const bundles=Array.from({length:3585},(_,bundle)=>({bundle,ports:Array.from({length:4},(_,port)=>({port,status_hex:'00'.repeat(11)}))}));
  const build=(fresh=false)=>{
    const schedule=[];let draw=0;
    for(let bundle=0;bundle<3585;bundle++){
      schedule.push({action:'tick',action_index:schedule.length,bundle});
      if(!(fresh?[2000,2500]:[2000,2500,3000]).includes(bundle))schedule.push({action:'draw',action_index:schedule.length,draw:draw++});
    }
    return {schema:fresh?'melee-web-stadium-fresh132-full-a-v1':'melee-web-stadium-run2-full-schedule-v3',version:fresh?1:3,
      bundles,schedule,checkpoints:{c_action_index:168,c_source_tick:84,f_action_index:170,f_source_tick:85,
        terminal_action_index:schedule.length-1,terminal_draw_ordinal:draw-1}};
  };
  const full=build(),fresh=build(true),prefix={...full,schema:'melee-web-stadium-run2-proper-prefix-v2',version:2,
    bundles:bundles.slice(0,87),schedule:full.schedule.slice(0,174),checkpoints:{...full.checkpoints,terminal_action_index:173,terminal_draw_ordinal:86}};
  for(const [name,payload] of [['encodeRun2NativeManifest',prefix],['encodeRun2FullNativeManifest',full],
    ['encodeRun2FullCommonNativeManifest',full],['encodeFresh132FullANativeManifest',fresh]])
    assert.deepEqual(consumer[name](payload),base[name](payload));
  const s=sources();assert.throws(()=>consumer.encodeRun2NativeManifest(s.runtimePayload));
  assert.throws(()=>consumer.encodeFresh132FullANativeManifest(s.runtimePayload));
  assert.throws(()=>new consumer.StadiumSetupCfActionDriver({runtimePayload:s.runtimePayload,native:{}}));
  assert.throws(()=>new consumer.StadiumSetupCfActionDriver({laterBProjection:raw,laterBToken:UUID,fresh132FullA:true}));
});
