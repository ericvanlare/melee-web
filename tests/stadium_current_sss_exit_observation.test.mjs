import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import {finalizeSessionCapture} from '../scripts/whole_session_capture_result.mjs';
import {installCurrentSssExitReplayArm, compareCurrentSssExitObservation,
  validateCurrentSssExitTrace, validateCurrentSssExitTerminal, validateCurrentSssExitFinalization,
  CURRENT_SSS_EXIT_REFUSAL} from '../scripts/stadium_current_sss_exit_observation.mjs';

// Endpoint doubles establish only wrapper order/descriptor/refusal behavior.
function fixture({result = 1, armResult = 1, thrown = null, replace = false} = {}) {
  const bytes = Array.from({length: 328}, (_, i) => i & 255), events = [];
  const heap = new Uint8Array(400); heap.set(bytes, 16);
  const module = {HEAPU8: heap, UTF8ToString: () => '{}',
    stadiumMatchedSetupNativeIdle: () => true,
    _melee_web_native_menu_stadium_current_sss_exit_arm() { events.push('arm'); return armResult; },
    _melee_web_native_menu_stadium_current_sss_exit_observe() { events.push('observe'); return 1; }};
  const receiver = {};
  function original(...args) {
    events.push('parse'); assert.equal(this, receiver); assert.deepEqual(args, [16, bytes.length, 1]);
    if (replace) module._melee_web_native_menu_replay = () => 9;
    if (thrown) throw thrown;
    return result;
  }
  Object.defineProperty(module, '_melee_web_native_menu_replay',
    {value: original, writable: true, enumerable: false, configurable: true});
  globalThis.Module = module;
  const before = Object.getOwnPropertyDescriptor(module, '_melee_web_native_menu_replay');
  const scope = installCurrentSssExitReplayArm({recipeBytes: bytes});
  const invoke = () => module._melee_web_native_menu_replay.call(receiver, 16, bytes.length, 1);
  return {module, scope, before, invoke, events};
}

test('synthetic wrapper calls real supplied original exactly once then arm, preserves descriptor', () => {
  const f = fixture(); assert.equal(f.invoke(), 1); f.events.push('launch');
  assert.deepEqual(f.events, ['parse', 'arm', 'launch']);
  assert.equal(f.scope.state().armed, true); assert.equal(f.scope.dispose(), true);
  assert.deepEqual(Object.getOwnPropertyDescriptor(f.module, '_melee_web_native_menu_replay'), f.before);
});
test('synthetic wrapper preserves exact original thrown object and refusal return', () => {
  const thrown = new Error('original failure'), f = fixture({thrown});
  assert.throws(f.invoke, error => error === thrown); assert.deepEqual(f.events, ['parse']); f.scope.dispose();
  const g = fixture({result: 0}); assert.equal(g.invoke(), 0); assert.deepEqual(g.events, ['parse']); g.scope.dispose();
});
test('synthetic arm refusal explicitly prevents ordinary launch', () => {
  const f = fixture({armResult: 0}); let launched = false;
  assert.throws(() => { f.invoke(); launched = true; }, /arm refused/);
  assert.equal(launched, false); assert.deepEqual(f.events, ['parse', 'arm']);
  assert.match(f.scope.state().failure, /launch suppressed/); f.scope.dispose();
});
test('synthetic foreign callback replacement is neither invoked nor restored', () => {
  const f = fixture(), foreign = () => 3;
  f.module._melee_web_native_menu_replay = foreign;
  assert.equal(f.scope.state().owned, false);
  assert.throws(() => f.scope.dispose(), /foreign replacement/);
  assert.equal(f.module._melee_web_native_menu_replay, foreign); assert.deepEqual(f.events, []);
});
test('synthetic replacement by original after its return refuses arm and leaves foreign callback', () => {
  const f = fixture({replace: true}); assert.throws(f.invoke, /lost its returned idle owner/);
  assert.deepEqual(f.events, ['parse']); assert.throws(() => f.scope.dispose(), /foreign replacement/);
});
test('synthetic foreign Module and repeated call cannot acquire diagnostic owner', () => {
  const f = fixture(); globalThis.Module = {...f.module}; assert.throws(f.invoke, /ownership/);
  assert.deepEqual(f.events, []); globalThis.Module = f.module; f.scope.dispose();
  const g = fixture(); g.invoke(); assert.throws(g.invoke, /reuse/); assert.deepEqual(g.events, ['parse', 'arm']);g.scope.dispose();
});
test('synthetic changed recipe and busy owner refuse arm; busy observation makes no native read', () => {
  const f = fixture(); f.module.HEAPU8[16] ^= 1; assert.throws(f.invoke, /foreign recipe/);
  assert.deepEqual(f.events, ['parse']);f.scope.dispose();
  let idle = true; const g = fixture();
  // Replace before installation in a separate real function capsule.
  g.scope.dispose();g.module.stadiumMatchedSetupNativeIdle = () => idle;
  const bytes = Array.from(g.module.HEAPU8.slice(16, 344));
  const scope = installCurrentSssExitReplayArm({recipeBytes: bytes});idle = false;
  assert.deepEqual(scope.observe(), {busy: true}); assert.deepEqual(g.events, []);
  assert.throws(g.invoke, /idle owner/); assert.deepEqual(g.events, ['parse']);scope.dispose();
});

const expectedPath = process.env.MELEE_STADIUM_CURRENT_SSS_EXPECTED;
const parsedPath = process.env.MELEE_STADIUM_CURRENT_PARSED_SOURCE;
test('retained source operands + synthetic native note exercise strict paired fields/order/terminal',
  {skip: !expectedPath || !parsedPath}, () => {
  const expected = JSON.parse(fs.readFileSync(expectedPath)), parsed = JSON.parse(fs.readFileSync(parsedPath));
  // Explicitly synthetic native endpoint: paired fields projected from retained
  // operands; unpaired ABI-shaped fields from actual parser output. Not live SSS.
  const start = structuredClone(parsed.typed_setup);
  for (const [key, value] of Object.entries(expected.expected.sss.vs.start.rules))
    start.rules[({hud_layout:'x0_3', item_frequency:'xB'})[key] || key] = typeof value === 'boolean' ? Number(value) : value;
  expected.expected.sss.vs.start.players.forEach((p, i) => Object.assign(start.players[i], p));
  const actual = {scope:'passive_sss_onexit_return_only',armed:true,notified:true,failed:false,error:null,
    notification_received:true,observation_state:2,menu_draw_reference:'UNKNOWN',runtime_admission:false,Entry:false,C3:false,
    notification_coordinates:{replay_cursor:expected.notification_cursor,callback_source_steps:0,callback_source_draws:0,
      order:expected.notification_cursor,enclosing_host_tick_status:'not_in_host_tick',last_host_tick_call_returned:true},
    native_sss_bytes_scope:'native_ABI_only_not_original_raw_equivalence',native_sss_bytes_hex:'00',
    note:{...structuredClone(expected.expected),host_entered:true,session_phase:3,source_scene:2,
      session_ticks:expected.notification_cursor,world_generation:3,audio_generation:1,
      owners:Object.fromEntries(['host','session','world','audio','vs_mode','scene_info','payload','seed'].map(k=>[k,true])),
      sss:{header:{unk_stage:0,x1:0,no_lras:0,force_stage_id:-1,start_game:1},vs:{start}}}};
  assert.equal(compareCurrentSssExitObservation(actual,expected).pass,true);
  for (const mutate of [a=>a.note.random_seed_hex='00000000',a=>a.note.pad_state_hex='00'.repeat(822),
    a=>a.note.owners.audio=false,a=>a.note.world_generation=0,a=>a.notification_coordinates.replay_cursor++,
    a=>a.note.sss.header.start_game=-1,a=>a.note.sss.vs.start.rules.damage_ratio_bits='00000000',
    a=>a.note.sss.vs.start.players[0].color++,a=>a.Entry=true]) {
    const changed=structuredClone(actual);mutate(changed);assert.throws(()=>compareCurrentSssExitObservation(changed,expected));
  }
  const rows=[{record:'header',schema:'melee-web-port-session-diagnostic',version:1,frames_requested:expected.recipe.frames,comparison:'not_run',draw_state:'not_captured'}];
  for (const r of expected.literal_inputs) {
    rows.push({record:'session_frame',index:r.index,scene:r.scene,supplied_inputs:r.pads,pad_state_hex:expected.expected.pad_state_hex,rng:1});
    rows.push({record:'current_sss_exit_order',order:r.index,kind:'tick_return',replay_cursor:r.index,source_steps:1,source_draws:0,menu_draw_reference:'UNKNOWN'});
  }
  const text=rows.map(r=>JSON.stringify(r)).join('\n');assert.equal(validateCurrentSssExitTrace(text,expected,actual).pass,true);
  for(const mutate of [r=>r.shift(),r=>r[1].supplied_inputs[0]='ff'.repeat(11),r=>r[1].index++,
    r=>r.push({...r[0]}),r=>r.push({record:'setup'})]) {
    const changed=structuredClone(rows);mutate(changed);assert.throws(()=>validateCurrentSssExitTrace(changed.map(r=>JSON.stringify(r)).join('\n'),expected,actual));
  }
  const terminal={pass:false,complete:false,frames:expected.recipe.frames,mode:'state_capture',scheduling_equivalence:'not_evaluated',instrumented_timing_resumes:0,metrics:{sourceFrames:expected.notification_cursor,sourceSteps:expected.notification_cursor,sourceDraws:0},source_match:{complete:false,outcome:null,winner:null},
    final_scene:null,failures:['whole-session final CSS was not entered',CURRENT_SSS_EXIT_REFUSAL,
      'incomplete input timeline','source tick/draw count mismatch']};
  assert.equal(validateCurrentSssExitTerminal(terminal,{phase:0,running:0},expected).pass,true);
  for(const mutate of [r=>r.pass=true,r=>r.complete=true,r=>r.failures.push('teardown incomplete'),
    r=>r.source_match.complete=true,r=>r.instrumented_timing_resumes=1,r=>r.metrics.sourceFrames++]) {const changed=structuredClone(terminal);mutate(changed);assert.throws(()=>validateCurrentSssExitTerminal(changed,{phase:0,running:0},expected));}
  assert.throws(()=>validateCurrentSssExitTerminal(terminal,{phase:2,running:0},expected));
});

const actualReportPath = process.env.MELEE_STADIUM_CURRENT_SSS_REPORT;
test('retained actual SSS finalization preserves TIME diagnostic and rejects additional failures',
  {skip: !actualReportPath || !expectedPath}, () => {
  const raw = JSON.parse(fs.readFileSync(actualReportPath));
  const expected = JSON.parse(fs.readFileSync(expectedPath));
  const first = structuredClone(raw.first_error);
  const report = structuredClone(raw);
  finalizeSessionCapture(report); // Actual unchanged default/full-route gate stays FAIL.
  assert.equal(report.result, 'fail');
  report.original_replay_capture_result = report.result;
  assert.equal(validateCurrentSssExitFinalization(report, expected).pass, true);
  assert.deepEqual(report.first_error, first, 'classification must preserve raw fatal diagnostic');
  assert.equal(report.result, 'fail', 'classifier cannot relabel the original replay');
  assert.equal(report.browser_report.pass, false);
  assert.equal(report.browser_report.complete, false);
  for (const mutate of [r=>r.first_error.message+=' extra', r=>r.first_error.kind='snapshot',
    r=>r.first_error.phase='disc-import', r=>r.failure='later failure',
    r=>r.first_mismatch={message:'later mismatch'},
    r=>r.snapshots.push({runtime_error:'later different runtime error'}),
    r=>r.final_snapshot.runtime_error='later different runtime error',
    r=>r.last_successful_snapshot.snapshot_error='later observation timeout',
    r=>r.phases.push({name:'later phase',result:'fail'}),
    r=>r.snapshots=undefined, r=>r.snapshots=[],
    r=>r.snapshots=Array.from({length:2048},()=>structuredClone(r.final_snapshot)),
    r=>r.browser_errors.push({message:'unexpected browser failure'}),
    r=>r.unexpected_requests.push({method:'POST'}),
    r=>r.close_error='browser cleanup failed', r=>r.cleanup.browser_close_returned=false,
    r=>r.final_snapshot.running=1, r=>r.browser_report.pass=true]) {
    const changed = structuredClone(raw);mutate(changed);
    finalizeSessionCapture(changed);changed.original_replay_capture_result=changed.result;
    assert.throws(()=>validateCurrentSssExitFinalization(changed,expected));
    assert.equal(changed.result,'fail','default/full-route finalization must remain failed');
  }
});
