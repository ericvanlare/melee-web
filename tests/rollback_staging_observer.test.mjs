import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import vm from 'node:vm';

// Execute the shipped observer with synthetic DOM, runtime, and native leaves.
const shell = fs.readFileSync(new URL('../web/online/online-shell.mjs', import.meta.url), 'utf8');
const start = 'export async function onlineSessionSnapshot(';
const end = 'window.onlineSessionSnapshot=onlineSessionSnapshot;';
assert.equal(shell.split(start).length, 2);
assert.equal(shell.split(end).length, 2);
const startAt = shell.indexOf(start);
const endAt = shell.indexOf(end);
assert(endAt > startAt);
const observer = shell.slice(startAt + 'export '.length, endAt);

function fixture() {
  const calls = [];
  const native = {phase: 7, running: 0, ownerState: 1, statusFailure: null};
  const owner = () => ({
    ownerState: native.ownerState, pendingCommand: 0, completeSequence: 1, requestSequence: 1,
    gpuPending: 0, endpointActive: 0, firstFailure: 0, rendererErrors: 0,
  });
  const report = {roomId: 'fixture-room', result: 'pending', failure: null};
  const world = vm.createContext({
    structuredClone, report, role: 'alpha', state: {}, roomUsed: true, discReady: true,
    startCommitted: true, historyOwned: false, nativeRetired: false, closed: false,
    loopWork: null, $: () => ({textContent: 'Fixture room status'}),
    controller: {status() {
      calls.push('typed status');
      if (native.statusFailure) throw native.statusFailure;
      return owner();
    }},
    context: {
      boundary(sample) { calls.push('ordinary boundary'); return sample(); },
      Module: {
        _melee_web_native_menu_phase() { calls.push('native phase'); return native.phase; },
        _melee_web_native_menu_running() { calls.push('native running'); return native.running; },
        _melee_web_input_message() { calls.push('native input'); return 1; },
        _melee_web_native_menu_match_observe() { calls.push('native match'); return 2; },
        UTF8ToString(pointer) { return pointer === 1 ? '{"ready":true}' : '{"frame":0}'; },
      },
      callbacks: {menuStagingDiagnosticsEnabled: () => true},
      diagnostics: {
        exportReports() { calls.push('current incidents'); return {incidents: []}; },
        async exportRetained() { calls.push('retained incidents'); return {records: []}; },
      },
    },
  });
  const snapshot = vm.runInContext(observer + '\nonlineSessionSnapshot', world);
  return {snapshot, world, report, calls, native};
}

test('committed start observes the transition without ordinary queue or native reads', async () => {
  const {snapshot, calls} = fixture();
  const value = await snapshot({source: true});
  assert.equal(value.stage, 'starting');
  assert.equal(value.phase, null);
  assert.equal(value.running, null);
  assert.equal(value.owner, null);
  assert.equal(value.input, undefined);
  assert.equal(value.stagingDiagnosticsEnabled, true);
  assert.deepEqual(calls, []);
});

test('typed history guard resumes reads after the same committed start takes ownership', async () => {
  const {snapshot, world, calls, native} = fixture();
  world.historyOwned = true;
  world.loopWork = Promise.resolve();
  native.statusFailure = Error('Runtime history Asyncify call is in progress');
  await assert.rejects(snapshot({source: true}), error => error === native.statusFailure);
  assert.deepEqual(calls.splice(0), ['typed status']);

  native.statusFailure = null;
  const value = await snapshot({source: true});
  assert.equal(value.stage, 'playing');
  assert.equal(value.phase, 7);
  assert.equal(value.input.ready, true);
  assert.equal(value.match.frame, 0);
  assert.deepEqual(calls, ['typed status', 'native phase', 'native running', 'native input', 'native match']);
});

test('a committed-start failure retains the JS report without touching either native owner', async () => {
  const {snapshot, world, report, calls} = fixture();
  report.failure = 'Fixture start commit refused';
  report.result = 'FAIL';
  report.closed = true;
  world.closed = true;
  const value = await snapshot({source: true, diagnostics: true});
  assert.equal(value.stage, 'closed');
  assert.equal(value.closed, true);
  assert.equal(value.error, report.failure);
  assert.equal(value.session.failure, report.failure);
  assert.equal(value.session.result, 'FAIL');
  assert.equal(value.owner, null);
  assert.deepEqual(value.incidents, {incidents: []});
  assert.deepEqual(value.retainedIncidents, {records: []});
  assert.deepEqual(calls, ['current incidents', 'retained incidents']);
});

test('ordinary menus use the queued boundary and a retired native owner bypasses it', async () => {
  const {snapshot, world, calls, native} = fixture();
  world.startCommitted = false;
  native.phase = 1;
  native.running = 1;
  native.ownerState = 0;
  world.context.Module._melee_web_native_menu_match_history_start_state = () => {
    calls.push('native start state');return 1;
  };
  const ordinary = await snapshot();
  assert.equal(ordinary.phase, 1);
  assert.equal(ordinary.running, 1);
  assert.equal(ordinary.startState, 1);
  assert.deepEqual(calls.splice(0), ['ordinary boundary', 'typed status', 'native phase', 'native running', 'native start state']);

  world.startCommitted = true;
  world.nativeRetired = true;
  world.closed = true;
  native.phase = 0;
  native.running = 0;
  native.ownerState = 2;
  const retired = await snapshot();
  assert.equal(retired.stage, 'closed');
  assert.equal(retired.phase, 0);
  assert.equal(retired.owner.ownerState, 2);
  assert.equal(retired.startState, null);
  assert.deepEqual(calls, ['typed status', 'native phase', 'native running']);
});


test('original Results reads trace and native message inside the ordinary owner boundary', async () => {
  const {snapshot, world, calls, native} = fixture();
  world.startCommitted = false;
  world.state.canPause = true;
  native.phase = 8;
  native.running = 1;
  native.ownerState = 2;
  const module = world.context.Module;
  module._melee_web_native_menu_message = () => 3;
  module._melee_web_native_menu_results_pad_trace = () => {
    calls.push('native Results trace');
    return 4;
  };
  const decode = module.UTF8ToString;
  module.UTF8ToString = pointer => pointer === 3 ? 'Original Results' :
    pointer === 4 ? '{"samples":[]}' : decode(pointer);
  const value = await snapshot({source:true});
  assert.equal(value.canPause, true);
  assert.equal(value.nativeMessage, 'Original Results');
  assert.equal(value.match.frame, 0);
  assert.deepEqual(value.results, value.match);
  assert.deepEqual(value.resultsTrace, {samples:[]});
  assert.equal(calls[0], 'ordinary boundary');
  assert(calls.includes('native Results trace'));
});

test('room launch is recorded before awaiting and cannot continue after Leave', async () => {
  const healthyStart = shell.indexOf('function healthy(){');
  const healthyEnd = shell.indexOf('async function until(', healthyStart);
  const launchStart = shell.indexOf('async function startRoomPlayer(){');
  const launchEnd = shell.indexOf('async function openRoom(', launchStart);
  assert(healthyStart >= 0 && healthyEnd > healthyStart && launchStart >= 0 && launchEnd > launchStart);
  let releaseStart;
  const launchPromise = new Promise(resolve => { releaseStart = resolve; });
  const continued = [];
  const world = vm.createContext({closed:false,state:{},launchWork:null,
    player:{start:()=>launchPromise}});
  const start = vm.runInContext(shell.slice(healthyStart, healthyEnd) +
    shell.slice(launchStart, launchEnd) + '\nstartRoomPlayer', world);
  const pending = start();
  assert.equal(world.launchWork, launchPromise,'cleanup must see startup before it settles');
  const next = pending.then(()=>continued.push('native setup'));
  world.closed = true;
  releaseStart();
  await assert.rejects(pending,/Room was closed/);
  await assert.rejects(next,/Room was closed/);
  assert.deepEqual(continued,[],'closed room cannot continue to native setup');
});

test('player unload waits for room endpoints and the in-flight launch to finish', async () => {
  const closeStart = shell.indexOf('async function close(){');
  const retireStart = shell.indexOf('function retireAfterClose(){', closeStart);
  const retireEnd = shell.indexOf('// Bounded, read-only diagnostics', retireStart);
  assert(closeStart >= 0 && retireStart > closeStart && retireEnd > retireStart);
  const events = [];
  let releaseStart, sdkBusy = true;
  const launchWork = new Promise(resolve => {
    releaseStart = () => { sdkBusy = false; events.push('launch settled'); resolve(); };
  });
  const elements = new Map();
  const world = vm.createContext({
    Promise, AggregateError, setTimeout, launchWork, prepareWork:null, loopWork:null,
    closed:false, stopMenuProgress:false, closeWork:null, retireWork:null,
    loop:{abortLateScenario(){}},bridge:{async close(){events.push('bridge close');}},
    setup:{async close(){events.push('setup close');}},
    controller:{status:()=>({ownerState:0})},historyOwned:false,nativeRetired:false,
    report:{},setupEvent(){},
    context:{},
    player:{async unload(){assert.equal(sdkBusy,false,'SDK unload must wait for player.start');events.push('player unload');}},
    $(id){if(!elements.has(id))elements.set(id,{});return elements.get(id);},
  });
  const functions = shell.slice(closeStart, retireEnd) + '\n({retireAfterClose})';
  const {retireAfterClose} = vm.runInContext(functions, world);
  const pending = retireAfterClose();
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.deepEqual(events,['bridge close','setup close'],
    'network endpoints close before waiting on the player launch');
  releaseStart();
  await pending;
  assert.deepEqual(events,['bridge close','setup close','launch settled','player unload']);
});
