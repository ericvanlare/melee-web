import test from 'node:test';
import assert from 'node:assert/strict';
import {frameEffectObservationFailures, compareFrameEffectObservations} from './match_frame_effect_driver.mjs';

import {schema2Fixture} from './match_frame_effect_schema2_fixture.mjs';

test('schema2 retains exact values and LOAD finding without restore acceptance', () => {
  const r = schema2Fixture();
  assert.deepEqual(frameEffectObservationFailures(r), []);
  assert.deepEqual(compareFrameEffectObservations(r, structuredClone(r)), []);
  assert.equal(r.renderer.first_blocker, 7);
  for (const change of [x => {x.schema_version = 1;}, x => {x.renderer.schema_version = 1;}]) {
    const x = structuredClone(r); change(x);
    assert(frameEffectObservationFailures(x).length > 0, 'schema1 is never coerced');
  }
});

test('missing/unknown/truncated/order/identity corruption refuses explicitly', () => {
  const changes = [x => {delete x.draws;}, x => {x.rows_complete = false;}, x => {x.resources.pop();},
    x => {x.binding_entries = [];}, x => {x.draws[0].unknown = 1;}, x => {delete x.draws[0].viewport_bits;},
    x => {x.renderer.unknown = 1;}, x => {x.renderer.resource_count = 6145;},
    x => {x.events[0].kind = 25;}, x => {x.events[0].flags = 5;},
    x => {x.events.reverse();}, x => {[x.events[2], x.events[3]] = [x.events[3], x.events[2]];},
    x => {x.draws[0].pipeline_id = '3';}, x => {x.draws[0].bind_group_ids[0] = '2';},
    x => {x.resources[0].capture_id = '99';}, x => {x.binding_entries[0].resource_id = '2';},
    x => {x.renderer.finding_mask = '0';}, x => {x.events[0].handle_a = '18446744073709551616';}];
  for (const change of changes) {
    const x = schema2Fixture(); change(x);
    assert(frameEffectObservationFailures(x).length > 0);
    assert(compareFrameEffectObservations(schema2Fixture(), x).length > 0);
  }
});

test('exact comparison detects bits and values outside direct draw-event fields', () => {
  const changes = [x => {x.draws[0].viewport_bits[0] = 0;}, // negative0 versus positive0
    x => {x.draws[0].viewport_bits[1]++;}, // distinct NaN payloads
    x => {x.draws[0].clear_color_bits[1] = '9221120237041090562';},
    x => {x.draws[0].dynamic_offsets[0][3]--;},
    x => {x.draws[0].pipeline_key = '18446744073709551614';},
    x => {x.binding_entries[0].offset = '9007199254740992';},
    x => {x.resources[0].words[0]--;},
    x => {[x.resources[0], x.resources[1]] = [x.resources[1], x.resources[0]];}];
  for (const change of changes) {
    const x = schema2Fixture(); change(x);
    assert(compareFrameEffectObservations(schema2Fixture(), x).some(f => f.startsWith('different:')));
  }
});

test('opaque depth owner/dimension fields are compared without false handle rebasing', () => {
  const r = schema2Fixture();
  const depth = {kind:22, frame_index:3, op_index:0, item_index:0, flags:6, value0:0, value1:1, value2:0, value3:640,
    handle_a:'2748779069920', handle_b:'9007199254740993'};
  r.events.splice(-2, 0, depth);r.renderer.event_count++;r.renderer.depth_trace_count++;
  assert.deepEqual(frameEffectObservationFailures(r), []);
  const changed = structuredClone(r);changed.events.at(-3).handle_b = '9007199254740994';
  assert(compareFrameEffectObservations(r, changed).some(f => f.startsWith('different:')));
});
