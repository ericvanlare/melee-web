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
