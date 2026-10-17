import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {armNativeFrameEffect, readNativeFrameEffect, retireNativeFrameEffect,
  frameEffectSchema1ObservationFailures as frameEffectObservationFailures} from './match_frame_effect_driver.mjs';

function fixture({idle = true, returned = true} = {}) {
  const calls = [];
  globalThis.Module = {
    matchFrameEffectNativeIdle: () => idle,
    matchFrameEffectCallbackReturned: returned,
    UTF8ToString: value => value,
    _melee_web_native_menu_match_observe: () => { calls.push('match'); return JSON.stringify({ready: true, frame: 200}); },
    _melee_web_native_menu_frame_effect_arm: frame => { calls.push(['arm', frame]); return 1; },
    _melee_web_native_menu_frame_effect_status: () => { calls.push('status'); return JSON.stringify({terminal: true, error: '', renderer: {fence_count: 1}, native_retired: true}); },
    ccall: async () => { calls.push('retire'); return 1; },
  };
  return calls;
}

test('actual generated-runtime guard refuses an unwound but pending native continuation', () => {
  const source = fs.readFileSync(new URL('../src/gameplay_menu_browser.cpp', import.meta.url), 'utf8');
  const assignment = source.match(/Module\.matchFrameEffectNativeIdle=\(\)=>[^;]+;/g);
  assert.equal(assignment?.length, 1);
  for (const [state, currData, abort, expected] of [[0,null,false,true],[0,123,false,false],[1,null,false,false],[0,null,true,false]]) {
    const context = {Module: {}, ABORT: abort, Asyncify: {state, currData, State: {Normal: 0}}};
    vm.runInNewContext(assignment[0], context);
    assert.equal(context.Module.matchFrameEffectNativeIdle(), expected);
  }
});

test('pending calls forbid native arm, getters and retirement', async () => {
  const calls = fixture({idle: false});
  assert.equal(armNativeFrameEffect().armed, false);
  assert.equal(readNativeFrameEffect().settled, false);
  assert.equal((await retireNativeFrameEffect()).retired, false);
  assert.deepEqual(calls, []);
});

test('callback return is required before getter and retirement', async () => {
  const calls = fixture({returned: false});
  assert.equal(readNativeFrameEffect().settled, false);
  assert.equal((await retireNativeFrameEffect()).retired, false);
  assert.deepEqual(calls, []);
});

test('arm binds the frame read in the same synchronous page call', () => {
  const calls = fixture();
  assert.equal(armNativeFrameEffect().armed, true);
  assert.deepEqual(calls, ['match', ['arm', 200]]);
});

test('retirement awaits the underlying call before any final native read', async () => {
  const calls = fixture();
  let settle;
  Module.ccall = () => { calls.push('retire'); return new Promise(resolve => { settle = resolve; }); };
  const pending = retireNativeFrameEffect();
  assert.deepEqual(calls, ['retire']);
  settle(1);
  assert.equal((await pending).retired, true);
  assert.deepEqual(calls, ['retire', 'status']);
});

test('settled promise with a pending native continuation does not authorize a getter', async () => {
  const calls = fixture();
  Module.ccall = async () => { calls.push('retire'); Module.matchFrameEffectNativeIdle = () => false; return 1; };
  assert.equal((await retireNativeFrameEffect()).retired, false);
  assert.deepEqual(calls, ['retire']);
});

test('only a complete normal-submit observation passes; ownership blockers remain explicit', () => {
  const event = (kind, flags = 0) => ({kind, flags, frame_index: 3, op_index: 0, item_index: 0,
    value0: 0, value1: 0, value2: 0, value3: 0, handle_a: '0', handle_b: '0'});
  const receipt = {scope: 'one_normal_callback_observation_only', schema_version: 1,
    requested: true, admitted: true, terminal: true, renderer_armed: true, resumable: false, error: '',
    tick_returns: 1, draw_returns: 1, planned_steps: 1, callback_steps: 1, callback_draws: 1,
    owner_generation: 1, requested_source_frame: 200, source_frame_before: 200, source_frame_after: 201,
    planned_audio_steps: 1, audio_returns: 1, pcm_frames: 533, audio_phase_before: 0, audio_phase_after: 20,
    raw_pad_bytes: Array(64).fill(0),
    renderer: {schema_version: 1, state: 4, submit_count: 1, fence_count: 1, fence_success: 1,
      first_blocker: 7, event_count: 2, handle_count: 0, target_frame_index: 3, frame_index: 3, frame_id: '9007199254740993'},
    events: [event(17), event(18, 1)], handles: []};
  assert.deepEqual(frameEffectObservationFailures(receipt), []);
  assert.equal(receipt.renderer.first_blocker, 7);
  const typedRows = [
    {...event(1), handle_a: '4194304', handle_b: '700'},
    {...event(7), handle_a: '18446744073709551615'},
    {...event(8), handle_a: '9007199254740993'},
    {...event(5), handle_a: '1', handle_b: '1', value0: 1},
    event(17), event(18, 1),
  ];
  const typed = {...receipt, renderer: {...receipt.renderer, event_count: typedRows.length, handle_count: 1},
    events: typedRows, handles: [{capture_id: '1', kind: 2, reserved: 0}]};
  assert.deepEqual(frameEffectObservationFailures(typed), []);
  const badTlut = structuredClone(typed); badTlut.events[3].value0 = 2;
  assert(frameEffectObservationFailures(badTlut).includes('unlisted_handle'));
  for (const delta of [{source_frame_after: 202}, {audio_returns: 0}, {events: []}, {resumable: true},
    {pcm_frames: 534}, {raw_pad_bytes: []}, {events: undefined, handles: undefined},
    {events: [{}, {}]}, {events: [null, event(18, 1)]},
    {events: [event(19), event(18, 1)]}, {events: [event(17), event(18)]},
    {events: [event(18, 1), event(17)]}, {events: [event(17), event(17)]},
    {events: [event(17), {...event(18, 1), frame_index: 4}]},
    {events: [event(17), {...event(18, 1), value0: -1}]},
    {renderer: {...receipt.renderer, event_count: 3}, events: [{...event(3), handle_a: '1'}, event(17), event(18, 1)]},
    {events: [event(17), {...event(18, 1), handle_a: '18446744073709551616'}]},
    {requested_source_frame: '20', source_frame_before: '20', source_frame_after: '201'},
    {renderer: {...receipt.renderer, frame_id: '18446744073709551616'}},
    {renderer: {...receipt.renderer, target_frame_index: 4}},
    {renderer: {...receipt.renderer, submit_count: 0}}, {renderer: {...receipt.renderer, state: 5}}])
    assert(frameEffectObservationFailures({...receipt, ...delta}).length > 0);
});
