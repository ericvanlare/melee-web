import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {parseHitTransitionProbe,validateHitTransitionProbeRows} from '../scripts/rng_draw_probe.mjs';
const selection=parseHitTransitionProbe('5238,5239,5240');
assert.equal(parseHitTransitionProbe(undefined),null);
for(const text of ['5238','5239,5238,5240','5238,5239,5240,5241','+5238,5239,5240','5238,5239,5239'])
  assert.throws(()=>parseHitTransitionProbe(text));
const base={slot:1,entity_index:0,match_index:0,generation:3,fighter_player_id:1,
 fighter_gobj_linked:true,rng:'12345678',motion:17,animation:9,gate_x221f_b3:false,
 gate_x2219_b1:false,damage_bits:'00000000',damage_temp_bits:'80000000',damage_applied:0,
 knockback_bits:['00000000','00000000','00000000'],hitlag_bits:'00000000',
 source_gobj:'0000000000001234',source_slot:-1,source_player:6,time_since_hit:-1,x18a0_bits:'80000000'};
const row=cursor=>({schema:'melee-web-hit-transition-probe',version:1,source_cursor:cursor,
 overflowed:false,hook_counts:[0,0,0],events:[
 {...base,sequence:0,phase:'scheduler_start',kind:3,invocation:0},
 {...base,sequence:1,phase:'scheduler_return',kind:3,invocation:0}]});
const rows=selection.selected.map(row);
const validate=values=>validateHitTransitionProbeRows(values.map(JSON.stringify),selection,{observedCursor:5240});
assert.equal(validate(rows).complete,true);
for(const mutate of [
 r=>r.events[0].phase='entry',
 r=>r.events[1].phase='return',
 r=>r.events[1].sequence=0,
 r=>r.events[1].generation=4,
 r=>r.events[1].fighter_gobj_linked=false,
 r=>r.events[1].slot=0,
 r=>r.events[1].damage_bits='0',
 r=>r.events[1].gate_x221f_b3=0,
 r=>r.events[1].rng=123,
 r=>r.hook_counts[0]=1,
 r=>r.overflowed=true,
 r=>r.events=Array.from({length:65},()=>r.events[0]),
 r=>r.events[1].source_slot=4,
 r=>r.events[1].source_gobj='not-a-pointer',
 r=>r.events[1].generation=true,
]) {
 const changed=structuredClone(rows);mutate(changed[0]);assert.throws(()=>validate(changed));
}
assert.throws(()=>validate([rows[0],rows[0],rows[2]]));
assert.throws(()=>validate([rows[1],rows[0],rows[2]]));
assert.throws(()=>validate(rows.slice(1)),/omitted reached/);
// Execute the actual installed capture consumer with a synthetic window.
// This exercises callback storage/selection, not a browser or original payload.
const source=fs.readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
const start=source.indexOf('await page.addInitScript(({cpuObservationRowLimit');
const end=source.indexOf('  }, {cpuObservationRowLimit',start);
assert.ok(start>=0&&end>start);
const callbackText=source.slice(start+'await page.addInitScript('.length,end+3);
const context={window:{},console};
const callback=vm.runInNewContext('('+callbackText+')',context);
callback({cpuObservationRowLimit:1,captureCpuObservations:false,rngDrawProbeSelection:null,
 hitTransitionProbeSelection:selection,boundaryProbeEnabled:false});
assert.deepEqual(Array.from(context.window.__meleeHitTransitionProbeCursors),selection.selected);
context.window.meleeHitTransitionObservation(JSON.stringify(rows[0]));
assert.throws(()=>context.window.meleeHitTransitionObservation(JSON.stringify(rows[0])),/duplicated/);
assert.throws(()=>context.window.meleeHitTransitionObservation(JSON.stringify(row(5241))),/selected/);
callback({cpuObservationRowLimit:1,captureCpuObservations:false,rngDrawProbeSelection:null,
 hitTransitionProbeSelection:null,boundaryProbeEnabled:false});
assert.throws(()=>context.window.meleeHitTransitionObservation(JSON.stringify(rows[0])),/Unexpected/);
console.log('Actual selector, strict artifact validator and installed capture consumer passed');
