import test from 'node:test';
import assert from 'node:assert/strict';
import {parseRngDrawProbe, validateRngDrawProbeRows} from '../scripts/rng_draw_probe.mjs';

const cursors = parseRngDrawProbe({cursors: '12,13,27'});
assert.deepEqual(cursors, {request: {kind: 'cursors', cursors: [12, 13, 27]}, selected: [12, 13, 27]});
assert.deepEqual(parseRngDrawProbe({range: '40:42'}), {
  request: {kind: 'range', first: 40, last: 42}, selected: [40, 41, 42],
});
assert.equal(parseRngDrawProbe({}), null);
for (const request of [
  {range: '0:9'}, {range: '2:1'}, {range: '-1:2'}, {range: '0:108000'},
  {cursors: '1,1'}, {cursors: '2,1'}, {cursors: '1,,2'},
  {cursors: '0,1,2,3,4,5,6,7,8,9'}, {range: '1:2', cursors: '1,2'},
]) assert.throws(() => parseRngDrawProbe(request));

const row = (source_cursor, seed_after = '1234abcd') => JSON.stringify({
  schema: 'melee-web-rng-draw-probe', version: 1, source_cursor, overflowed: false,
  draws: [{kind: 'HSD_Randf', seed_after}],
});
const selection = parseRngDrawProbe({range: '8:9'});
assert.deepEqual(validateRngDrawProbeRows([row(8), row(9)], selection, {observedCursor: 9}), {
  rows: [row(8), row(9)], missing_cursors: [], complete: true,
});
assert.deepEqual(validateRngDrawProbeRows([row(8)], selection, {
  observedCursor: 8, deliberateStop: {requested_cursor: 8},
}), {rows: [row(8)], missing_cursors: [9], complete: false});
for (const invalidRows of [
  [row(8), row(8)],
  [row(9), row(8)],
  [row(8, '1234ABCd')],
  [JSON.stringify({schema: 'melee-web-rng-draw-probe', version: 1, source_cursor: 8,
    overflowed: true, draws: []})],
]) assert.throws(() => validateRngDrawProbeRows(invalidRows, selection, {observedCursor: 9}));
assert.throws(() => validateRngDrawProbeRows([row(8)], selection, {observedCursor: 9}), /omitted reached/);
console.log('Bounded RNG draw probe request and artifact contract passed');


test('version 2 owners are explicit and inconsistent or mixed owners refuse', () => {
  const selection = parseRngDrawProbe({cursors:'40,41'});
  const outside = {status:'outside_proc',gobj:'0000000000000000',proc:'0000000000000000',callback:'0000000000000000',classifier:0,p_link:0,s_link:0,callback_symbol:null};
  const scheduled = {status:'scheduled_proc',gobj:'0000000000010000',proc:'0000000000020000',callback:'0000000000000123',classifier:13,p_link:4,s_link:0,callback_symbol:'unknown'};
  const particle = {...scheduled,callback:'0000000000030123',classifier:8,p_link:11,s_link:15,callback_symbol:'efLib_particles_proc_main'};
  const fighter = {...scheduled,callback:'0000000000001926',classifier:4,p_link:8,s_link:1,callback_symbol:'Fighter_8006A360'};
  const ownerRow = (cursor, owner) => JSON.stringify({schema:'melee-web-rng-draw-probe',version:2,source_cursor:cursor,overflowed:false,draws:[{kind:'HSD_Rand',seed_after:'01234567',owner}]});
  const validate = rows => validateRngDrawProbeRows(rows, selection, {observedCursor:41});
  assert.equal(validate([ownerRow(40,outside),ownerRow(41,particle)]).complete,true);
  assert.equal(validate([ownerRow(40,outside),ownerRow(41,fighter)]).complete,true);
  for (const key of ['classifier','p_link','s_link'])
    assert.throws(()=>validate([ownerRow(40,outside),ownerRow(41,{...fighter,[key]:fighter[key]+1})]),/fighter callback owner tuple/);
  assert.throws(()=>validate([ownerRow(40,outside),ownerRow(41,{...fighter,callback_symbol:'Fighter_8006A364'})]),/callback symbol/);
  assert.throws(()=>validate([ownerRow(40,{...outside,callback:scheduled.callback}),ownerRow(41,scheduled)]),/outside-proc/);
  assert.throws(()=>validate([ownerRow(40,outside),ownerRow(41,{...scheduled,proc:outside.proc})]),/scheduled/);
  assert.throws(()=>validate([ownerRow(40,outside),row(41)]),/invalid identity/);
});
