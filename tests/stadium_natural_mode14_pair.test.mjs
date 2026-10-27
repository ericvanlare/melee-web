// Actual candidate functions; native callbacks below are labeled modeled controls.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';
const root=process.env.MELEE_SOURCE_ROOT;
assert.ok(root,'MELEE_SOURCE_ROOT required for source dependency resolution');
const modulePath=process.env.MELEE_NATURAL14_MODULE||path.join(root,'scripts/stadium_paired_v11_full.mjs');
let source=fs.readFileSync(modulePath,'utf8');
source=source.replaceAll("'../tests/stadium_matched_setup_cf_consumer.mjs'",JSON.stringify(pathToFileURL(path.join(root,'tests/stadium_matched_setup_cf_consumer.mjs')).href))
 .replaceAll("'./rng_draw_probe.mjs'",JSON.stringify(pathToFileURL(path.join(root,'scripts/rng_draw_probe.mjs')).href));
const {derivePairedFull,PairedFullComparator,installPairedFullLaunch}=await import('data:text/javascript;base64,'+Buffer.from(source).toString('base64'));
const projection=process.env.MELEE_NATURAL14_PROJECTION,report=process.env.MELEE_NATURAL14_REPORT;
if((projection===undefined)!==(report===undefined)||[projection,report].some(x=>x!==undefined&&!x))throw Error('Natural14 fixtures require both nonempty paths');
function fixture(){const p=JSON.parse(fs.readFileSync(projection)),ep=JSON.parse(fs.readFileSync(report));return {p,ep,e:derivePairedFull(p,p.recipe_sha256,p.actual_frame_count,ep)};}
test('new natural14 launch passes exact policy operands once; busy refuses before native',()=>{
 let arms=[],launches=0;globalThis.Module={stadiumMatchedSetupNativeIdle:()=>true,_melee_web_native_menu_launch(){launches++;},_melee_web_native_menu_stadium_paired_v11_natural_mode14_pair_arm(...a){arms.push(a);return 1;}};
 installPairedFullLaunch({natural14:true,mode14SourceTick:3725,lastSelectedSourceTick:3727});Module._melee_web_native_menu_launch();assert.deepEqual(arms,[[3725,3727]]);assert.equal(launches,1);assert.throws(()=>Module._melee_web_native_menu_launch(),/one-use/);
 globalThis.Module={stadiumMatchedSetupNativeIdle:()=>false,_melee_web_native_menu_launch(){throw Error('native forbidden');},_melee_web_native_menu_stadium_paired_v11_natural_mode14_pair_arm(){throw Error('native forbidden');}};
 installPairedFullLaunch({natural14:true,mode14SourceTick:3725,lastSelectedSourceTick:3727});assert.throws(()=>Module._melee_web_native_menu_launch(),/not settled/);
 assert.throws(()=>installPairedFullLaunch({natural14:true,stockout:true}),/explicit/);assert.throws(()=>installPairedFullLaunch({natural14:true,mode14SourceTick:3725,lastSelectedSourceTick:0x100000000}),/coordinates/);delete globalThis.Module;
});
test('actual retained natural14 projection admits exact recipe and preserves distinct scope',{skip:!projection},()=>{
 const {p,e}=fixture();assert.equal(e.natural14,true);assert.equal(e.stockout,undefined);assert.equal(e.matches[0].reference.states.length,0);assert.equal(e.matches[1].reference.states.length,3644);assert.equal(e.selection.selected.length,e.total);assert.equal(p.source_posttransform_outcome,'INCONCLUSIVE_MODE14_BEFORE_TRANSFORM');
});
test('actual retained admission refuses missing/reordered/foreign inputs and transformed witness',{skip:!projection},()=>{
 const {p,ep}=fixture();for(const mutate of [x=>x.recipe_sha256='0'.repeat(64),x=>x.matches.reverse(),x=>x.matches[1].actions[1].action_index++,x=>x.matches[1].states.pop(),x=>x.matches[1].states[1].pads[0]='f'.repeat(22),x=>x.matches[1].states[3725-84].state.map2.phase_s16=1,x=>x.matches[1].last_selected_source_tick=3726,x=>x.matches[1].mode14_source_tick=3724,x=>x.matches[1].standings_witness.neutral_release_observed=false,x=>x.matches[1].states.at(-1).pads[0]='f'.repeat(22),x=>x.matches[1].states[10].state.map2.phase_s16=1,x=>x.scene_rng_endpoints.samples[7].value++]){const q=structuredClone(p);mutate(q);assert.throws(()=>derivePairedFull(q,p.recipe_sha256,p.actual_frame_count,ep));}
});
test('actual state callback compares typed value seed PAD and owner; closure requires real release',{skip:!projection},async()=>{
 const {e}=fixture(),r=e.matches[1].reference;
 const {typedOriginalValues}=await import(pathToFileURL(path.join(root,'tests/stadium_matched_setup_cf_consumer.mjs')));
 function control(){const c=new PairedFullComparator(e);c.matches[0].css=true;c.matches[0].generation=3;Object.assign(c.matches[1],{generation:7,setup:true,go:true,state:0});const s=r.states[0],payload={ordinal:0,selected:true,typed22:{...typedOriginalValues(s.state,s.tick.source_tick),schema:'melee-web-stadium-per-tick-common-fields-native-v3',world_generation:7,source_tick:s.tick.source_tick,world_ticks:s.tick.source_tick+1,seed_hex:s.rng.value_hex},pad_queue:{schema:'melee-web-stadium-hsd-pad-queue-observation',version:2,source_generation:7,source_tick_before:s.tick.source_tick,source_tick_after:s.tick.source_tick+1,qnum_before:1,qread_before:0,qcount_before:1,qcount_after_hsd:0,tick_complete:true,queue_slot_status_hex:s.pads}};return {c,row:{record:'stadium_paired_v11',schema:'melee-web-stadium-paired-v11-row-v1',order:0,match_index:1,world_generation:7,diagnostic_action_index:0,kind:'state',payload}};}
 let x=control();x.c.accept(x.row);assert.equal(x.c.matches[1].state,1);
 for(const mutate of [x=>x.payload.typed22.seed_hex='0'.repeat(8),x=>x.payload.pad_queue.queue_slot_status_hex=['bad'],x=>x.world_generation=8,x=>x.payload.typed22[Object.keys(typedOriginalValues(r.states[0].state,r.states[0].tick.source_tick))[0]]=999]){x=control();mutate(x.row);assert.throws(()=>x.c.accept(x.row));}
 x=control();x.c.matches[1].state=r.states.length;x.row.kind='window_closed';x.row.payload={close_policy:'observed_mode14_and_neutral_release',state_samples:r.states.length,natural_tail_ticks:0,mode14_source_tick:3725,last_selected_source_tick:3727,mode14_observed:true,neutral_release_observed:false};assert.throws(()=>x.c.accept(x.row),/closure/);
});

test('preserved actual stockout admission remains a distinct unchanged policy',{skip:!process.env.MELEE_STOCKOUT_PROJECTION},()=>{
 assert.ok(process.env.MELEE_STOCKOUT_REPORT);const p=JSON.parse(fs.readFileSync(process.env.MELEE_STOCKOUT_PROJECTION)),ep=JSON.parse(fs.readFileSync(process.env.MELEE_STOCKOUT_REPORT));const e=derivePairedFull(p,p.recipe_sha256,p.actual_frame_count,ep);assert.equal(e.stockout,true);assert.equal(e.natural14,undefined);assert.equal(e.matches[1].reference.states.length,1537);
});
test('actual supervisor refusal is retained; forged completion and changed source refuse',{skip:!process.env.MELEE_NATURAL14_SUPERVISOR},()=>{
 assert.ok(process.env.MELEE_NATURAL14_EXPORTER&&process.env.MELEE_PYTHON);
 const script=`import runpy,json,copy,sys
m=runpy.run_path(sys.argv[1]);x=json.load(open(sys.argv[2]));m['validate_source_supervisor'](x)
for key,value in [('completed',True),('primary_error',None),('native_group_absent',False),('source_head','0'*40)]:
 y=copy.deepcopy(x);y[key]=value
 try:m['validate_source_supervisor'](y)
 except ValueError:pass
 else:raise AssertionError(key)
`;
 const r=spawnSync(process.env.MELEE_PYTHON,['-c',script,process.env.MELEE_NATURAL14_EXPORTER,process.env.MELEE_NATURAL14_SUPERVISOR],{encoding:'utf8'});assert.equal(r.status,0,r.stderr||r.stdout);
});
