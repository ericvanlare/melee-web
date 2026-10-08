import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import {installNetSourceAccounting, readNetSourceAccounting,
  verifyNetSourceAccounting} from '../scripts/net_source_accounting.mjs';
import {createNetLockstepNativeAdapter} from '../scripts/net_lockstep_native_adapter.mjs';
import {PAGE_HELPERS} from '../scripts/net_session_instance.mjs';
import {readyRenderEvent, verifyAccountedRenderReadiness} from '../scripts/net_lockstep_observers.mjs';

function fixture(original = () => 'original-result') {
  const native = {active: 1, cursor: 0, blocker: 'start_identity'};
  const window = {__net: {status: () => ({...native})}, menuRuntimeTiming: original};
  const context = vm.createContext({window});
  const page = {evaluate: async (fn, value) => {
    context.argument = value;
    return JSON.parse(JSON.stringify(vm.runInContext(`(${fn.toString()})(argument)`, context)));
  }};
  return {native, window, page, original};
}
const callback = (frame, steps, draws = steps) =>
  ({frame, valid: 1, source_steps: steps, source_draws: draws});

test('actual accounting owner retains a final real draw after the 4k text tail loses readiness', async () => {
  let log = '';
  const f = fixture(data => {
    log += data.source_draws ? `Native callback ${JSON.stringify({...data, source: 'Original character select'})}\n` :
      `Browser callback gap ${JSON.stringify({native: data, context: 'idle held source boundary '.repeat(20)})}\n`;
    return 'original-result';
  });
  f.window.__net.native = () => ({phase: 1, running: 1, error: null});
  f.window.__net.renderSource = () => 'Original character select';
  await installNetSourceAccounting(f.page, 32768, {retainRenderReadiness: true});
  for (let frame = 1; frame <= 8; ++frame) {
    f.native.cursor = frame;
    f.window.menuRuntimeTiming({...callback(frame, 1), began: 1, drawn: 1,
      preparation_ms: 0, draw_suppressed: 0, draw_calls: 297});
  }
  f.native.blocker = 'complete';
  for (let frame = 9; frame <= 88; ++frame) f.window.menuRuntimeTiming(callback(frame, 0));
  const diagnostics = {phase: 1, running: 1, status: 'Local game data loaded.', log: log.slice(-4000)};
  assert.equal(diagnostics.log.length, 4000);
  assert.equal(readyRenderEvent(diagnostics, 1), null);
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(verifyNetSourceAccounting(capture, 8).source_draws, 8);
  assert.equal(capture.render_readiness?.frame, 8);
  assert.equal(capture.render_readiness?.source_cursor, 8);
  assert.equal(capture.render_readiness?.phase, 1);
  const readiness = verifyAccountedRenderReadiness(capture, f.window.__net.native(), f.native, 8);
  assert.equal(readiness.frame, 8); assert.equal(readiness.draw_calls, 297);
  assert.equal(f.window.menuRuntimeTiming, f.original);
});

async function renderFixture(source = 'Original character select') {
  const f = fixture();
  f.window.__net.native = () => ({phase: 1, running: 1, error: null});
  f.window.__net.renderSource = () => source;
  await installNetSourceAccounting(f.page, 32768, {retainRenderReadiness: true});
  f.native.cursor = 8; f.native.blocker = 'complete';
  const row = {...callback(1, 8), began: 1, drawn: 1, preparation_ms: 0, draw_suppressed: 0, draw_calls: 297};
  return {...f, row};
}

test('structured readiness rejects missing, stale, wrong-phase and invalid actual context', async () => {
  const f = await renderFixture(); f.window.menuRuntimeTiming(f.row);
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  const mutations = [
    c => { c.render_readiness = null; }, c => { delete c.render_readiness_enabled; },
    c => { c.render_readiness.source_cursor = 7; }, c => { c.render_readiness.phase = 2; },
    c => { c.render_readiness.row_index = 5; }, c => { c.render_readiness.frame = 2; },
    c => { c.render_readiness.draw_calls = 0; }, c => { c.render_readiness.draw_calls = 0.5; },
    c => { c.render_readiness.source_draws = 0; }, c => { c.render_readiness.began = 0; },
    c => { c.render_readiness.drawn = 0; }, c => { c.render_readiness.preparation_ms = 1; },
    c => { c.render_readiness.draw_suppressed = 1; }, c => { c.render_readiness.source = 'preparing'; },
    c => { c.render_readiness.source = 'ready: 0'; }, c => { c.render_readiness.source = 'x'.repeat(4097); },
    c => { c.render_readiness.error = 'actual error'; }, c => { c.errors.push('observer failure'); },
  ];
  for (const mutate of mutations) {
    const bad = structuredClone(capture); mutate(bad);
    assert.throws(() => verifyAccountedRenderReadiness(bad, f.window.__net.native(), f.native, 8));
  }
  for (const native of [{phase: 2, running: 1, error: null}, {phase: 1, running: 0, error: null},
    {phase: 1, running: 1, error: 'actual error'}])
    assert.throws(() => verifyAccountedRenderReadiness(capture, native, f.native, 8));
  assert.throws(() => verifyAccountedRenderReadiness(capture, f.window.__net.native(), {...f.native, cursor: 9}, 8));
  const older = structuredClone(capture); older.rows = [callback(1, 7), callback(2, 1)];
  older.render_readiness = {...older.render_readiness, frame: 1, source_steps: 7, source_draws: 7};
  assert.throws(() => verifyAccountedRenderReadiness(older, f.window.__net.native(), f.native, 8));
});

test('structured getter/4096 overflow failures retain exact rows before waking and preserve error precedence', async () => {
  for (const failure of ['getter', 'overflow']) {
    const f = await renderFixture(failure === 'overflow' ? 'x'.repeat(4097) : 'source');
    const getterError = Error('actual source getter failure');
    if (failure === 'getter') f.window.__net.renderSource = () => { throw getterError; };
    let notified;
    const stop = f.window.__netSourceAccounting.subscribeProgress(error => {
      assert.equal(f.window.__netSourceAccounting.read(false).rows.length, 1);
      notified = error; throw Error('subscriber secondary failure');
    });
    assert.throws(() => f.window.menuRuntimeTiming(f.row), failure === 'getter' ? error => error === getterError : /4096/);
    assert(notified); stop();
    const captured = await readNetSourceAccounting(f.page, {freeze: true});
    assert.equal(captured.rows.length, 1); assert.equal(captured.render_readiness, null);
    assert.equal(captured.errors.length, 2);
  }
  const exact = Error('original exact failure');
  // A separate actual owner starts with the throwing original, never replacing
  // an already-installed owner in order to make a fixture pass.
  const broken = fixture(() => { throw exact; });
  broken.window.__net.native = () => { throw Error('getter must not run after original fails'); };
  broken.window.__net.renderSource = () => 'source';
  await installNetSourceAccounting(broken.page, 32768, {retainRenderReadiness: true});
  const stop = broken.window.__netSourceAccounting.subscribeProgress(error => { assert.equal(error, exact); throw Error('secondary'); });
  assert.throws(() => broken.window.menuRuntimeTiming({...callback(1, 8), began: 1, drawn: 1,
    preparation_ms: 0, draw_suppressed: 0, draw_calls: 297}), error => error === exact); stop();
  const failed = await readNetSourceAccounting(broken.page, {freeze: true});
  assert.equal(failed.rows.length, 1); assert.equal(failed.render_readiness, null);
});

test('structured source text4096 is accepted and legacy observer skips readiness getters', async () => {
  const f = await renderFixture('x'.repeat(4096)); f.window.menuRuntimeTiming(f.row);
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(verifyAccountedRenderReadiness(capture, f.window.__net.native(), f.native, 8).source.length, 4096);
  const legacy = fixture(); legacy.window.__net.native = () => { throw Error('unused'); };
  await installNetSourceAccounting(legacy.page); legacy.window.menuRuntimeTiming(callback(1, 1));
  const old = await readNetSourceAccounting(legacy.page, {freeze: true});
  assert.equal('render_readiness' in old, false);
});

test('actual page observer preserves callbacks and accounts preparation, catch-up and final draws', async () => {
  const calls = [];
  const f = fixture(function(data) { calls.push([this, data]); return 'original-result'; });
  await installNetSourceAccounting(f.page);
  for (const row of [callback(41, 0), callback(42, 2), callback(43, 6), callback(44, 0)])
    assert.equal(f.window.menuRuntimeTiming(row), 'original-result');
  f.native.cursor = 8; f.native.blocker = 'complete';
  const captured = await readNetSourceAccounting(f.page, {freeze: true});
  const verified = verifyNetSourceAccounting(captured, 8);
  assert.equal(verified.source_steps, 8);
  assert.equal(verified.source_draws, 8);
  assert.equal(verified.callback_count, 4);
  assert.equal(f.window.menuRuntimeTiming, f.original);
  assert.equal(calls.length, 4);
  assert.equal(calls[0][0], f.window);
  assert.equal((await readNetSourceAccounting(f.page, {freeze: true})).rows.length, 4);
});

test('runtime-owned accounting installs before native begin and starts at the captured identity barrier', async () => {
  const f = fixture();
  f.window.__net.native = () => ({phase: 1, running: 1, error: null});
  f.window.__net.renderSource = () => 'Original character select';
  f.native.active = 0;
  f.native.blocker = 'idle';
  const installed = await installNetSourceAccounting(f.page, 32768,
    {retainRenderReadiness: true, awaitStartIdentity: true});
  assert.equal(installed.initial, null);
  assert.equal(installed.awaiting_start_identity, true);
  f.window.menuRuntimeTiming(callback(1, 0)); // preparation callback before lockstep begins
  f.native.active = 1;
  f.native.blocker = 'start_identity';
  f.window.menuRuntimeTiming(callback(2, 0));
  assert.equal(f.window.__netSourceAccounting.read(false).initial.blocker, 'start_identity');
  f.native.cursor = 1;
  f.window.menuRuntimeTiming({...callback(3, 1), began: 1, drawn: 1,
    preparation_ms: 0, draw_suppressed: 0, draw_calls: 297});
  f.native.blocker = 'complete';
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.deepEqual(capture.rows.map(row => row.frame), [2, 3]);
  assert.equal(verifyNetSourceAccounting(capture, 1).source_steps, 1);
  assert.equal(capture.render_readiness?.frame, 3);
});

test('runtime-owned accounting fails if native ticks pass identity before its first observer row', async () => {
  const f = fixture();
  f.native.active = 0;
  f.native.blocker = 'idle';
  await installNetSourceAccounting(f.page, 32768, {awaitStartIdentity: true});
  f.native.active = 1;
  f.native.cursor = 1;
  f.native.blocker = 'network_wait';
  f.window.menuRuntimeTiming(callback(2, 1));
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.ok(capture.errors.some(error => /passed the start-identity barrier/.test(error)));
  assert.throws(() => verifyNetSourceAccounting(capture, 1), /incomplete/);
});

test('accounting fails closed on missing, repeated, reordered, invalid or uneven draws', async () => {
  for (const rows of [
    [callback(1, 1), callback(3, 1)],
    [callback(1, 1), callback(1, 1)],
    [callback(2, 1), callback(1, 1)],
    [callback(1, 1, 0), callback(2, 1, 2)],
    [{...callback(1, 2), valid: 0}],
    [{...callback(1, 2), source_draws: undefined}],
    [callback(1, 1)],
  ]) {
    const f = fixture(); await installNetSourceAccounting(f.page);
    for (const row of rows) f.window.menuRuntimeTiming(row);
    f.native.cursor = 2;
    assert.throws(() => verifyNetSourceAccounting(undefined, 2));
    const captured = await readNetSourceAccounting(f.page, {freeze: true});
    assert.throws(() => verifyNetSourceAccounting(captured, 2));
  }
});

test('capacity overflow and observer errors cannot become complete evidence', async () => {
  const f = fixture(); await installNetSourceAccounting(f.page, 1);
  f.window.menuRuntimeTiming(callback(1, 1));
  f.window.menuRuntimeTiming(callback(2, 1)); f.native.cursor = 2;
  const captured = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(captured.overflow, 1);
  assert.throws(() => verifyNetSourceAccounting(captured, 2), /incomplete/);

  const broken = fixture(() => { throw Error('original failure'); });
  await installNetSourceAccounting(broken.page);
  assert.throws(() => broken.window.menuRuntimeTiming(callback(1, 1)), /original failure/);
  broken.native.cursor = 1;
  const failed = await readNetSourceAccounting(broken.page, {freeze: true});
  assert.equal(failed.rows.length, 1);
  assert.match(failed.errors[0], /Original timing observer failed/);
  assert.throws(() => verifyNetSourceAccounting(failed, 1), /incomplete/);
});

test('measurement requires tick-zero barrier and retains foreign observer ownership', async () => {
  const started = fixture(); started.native.cursor = 1;
  await assert.rejects(installNetSourceAccounting(started.page), /unconsumed identity barrier/);
  const f = fixture(); await installNetSourceAccounting(f.page);
  await assert.rejects(installNetSourceAccounting(f.page), /already installed/);
  const foreign = () => {};
  f.window.menuRuntimeTiming = foreign;
  const captured = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(f.window.menuRuntimeTiming, foreign);
  assert.throws(() => verifyNetSourceAccounting(captured, 0), /incomplete/);
});


test('diagnostic progress subscriber follows original observer and retained exact row, then unsubscribes', async () => {
  const order = [], f = fixture(() => { order.push('original'); return 'original-result'; });
  await installNetSourceAccounting(f.page);
  const unsubscribe = f.window.__netSourceAccounting.subscribeProgress(error => {
    assert.equal(error, null); order.push('subscriber');
    assert.equal(f.window.__netSourceAccounting.read(false).rows.length, 1);
  });
  assert.equal(f.window.menuRuntimeTiming(callback(1, 2)), 'original-result');
  assert.deepEqual(order, ['original', 'subscriber']);
  unsubscribe(); f.native.cursor = 2;
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(verifyNetSourceAccounting(capture, 2).source_draws, 2);
  assert.equal(f.window.menuRuntimeTiming, f.original);
});

test('subscriber throw is retained and does not mask original observer failure or lose its row', async () => {
  const originalError = Error('original exact failure'), subscriberError = Error('subscriber exact failure');
  const f = fixture(() => { throw originalError; }); await installNetSourceAccounting(f.page);
  const unsubscribe = f.window.__netSourceAccounting.subscribeProgress(error => {
    assert.equal(error, originalError); throw subscriberError;
  });
  assert.throws(() => f.window.menuRuntimeTiming(callback(1, 1)), error => error === originalError);
  unsubscribe(); const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(capture.rows.length, 1); assert.equal(capture.errors.length, 2);
  assert.match(capture.errors[0], /Original timing observer failed/);
  assert.match(capture.errors[1], /Native progress subscriber failed/);
  const success = fixture(); await installNetSourceAccounting(success.page);
  const stop = success.window.__netSourceAccounting.subscribeProgress(() => { throw subscriberError; });
  assert.throws(() => success.window.menuRuntimeTiming(callback(1, 1)), error => error === subscriberError);
  stop(); assert.equal((await readNetSourceAccounting(success.page, {freeze: true})).rows.length, 1);
});

test('active subscriber freeze, foreign/frozen observer and duplicate owners fail sticky', async () => {
  const f = fixture(); await installNetSourceAccounting(f.page);
  const stop = f.window.__netSourceAccounting.subscribeProgress(() => {});
  await assert.rejects(readNetSourceAccounting(f.page, {freeze: true}), /active native progress subscriber/);
  assert.throws(() => f.window.__netSourceAccounting.subscribeProgress(() => {}), /already owned/);
  stop(); const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(capture.errors.length, 2);
  assert.throws(() => f.window.__netSourceAccounting.subscribeProgress(() => {}), /frozen/);
  const foreign = fixture(); await installNetSourceAccounting(foreign.page);
  const unsubscribe = foreign.window.__netSourceAccounting.subscribeProgress(() => {});
  foreign.window.menuRuntimeTiming = () => {};
  assert.throws(unsubscribe, /changed before unsubscribe/);
  assert.throws(() => foreign.window.__netSourceAccounting.subscribeProgress(() => {}), /changed/);
});


test('actual native peer diagnostic adapter uses accounting subscriber without replacing callback ownership', async () => {
  const f = fixture(); await installNetSourceAccounting(f.page);
  const observer = f.window.menuRuntimeTiming;
  const heap = new Uint8Array(2048), pushed = [], indexed = [];
  const Module = {
    HEAPU8: heap, _malloc: () => 8, _free() {},
    _melee_web_net_push(pointer, count) {
      pushed.push(heap.slice(pointer, pointer + count * 44)); return 1;
    },
    _melee_web_net_push_indexed(tick, pointer, count) {
      indexed.push({tick, bytes: heap.slice(pointer, pointer + count * 44)}); return 1;
    },
    _melee_web_net_enable_local_input_capture() { return 1; }, _melee_web_net_confirm_start() { return 1; },
    _melee_web_net_terminate() {}, _melee_web_net_checksum_drain() { return 0; },
    _melee_web_net_status() { return 0; }, UTF8ToString() { return '{"active":1,"cursor":0,"blocker":"start_identity"}'; },
  };
  await vm.runInNewContext(`(${PAGE_HELPERS.toString()})(() => createNetLockstepNativeAdapter)`,
    {window: f.window, Module, createNetLockstepNativeAdapter, atob, btoa});
  const native = f.window.__meleeWebNetNativePeerApi();
  const frame = Uint8Array.from({length: 44}, (_, index) => index ^ 0xa5);
  assert.equal(f.window.__net.push(Buffer.from(frame).toString('base64')), true,
    'the A1 bridge publishes decoded bytes through the shared adapter');
  assert.equal(native.pushIndexed(9, frame), true,
    'the A3 page peer publishes typed PAD frames through the same adapter');
  assert.deepEqual(pushed, [frame]);
  assert.deepEqual(indexed, [{tick: 9, bytes: frame}]);
  let calls = 0;
  const unsubscribe = native.subscribeProgress(error => { assert.equal(error, null); ++calls; });
  assert.equal(f.window.menuRuntimeTiming, observer);
  f.window.menuRuntimeTiming(callback(1, 1)); assert.equal(calls, 1);
  unsubscribe(); f.window.menuRuntimeTiming(callback(2, 1)); assert.equal(calls, 1);
  assert.equal(f.window.menuRuntimeTiming, observer);
});

test('invalid callback and overflow retain rows and notify the subscriber with explicit accounting failure', async () => {
  const f = fixture(); await installNetSourceAccounting(f.page, 1);
  const errors = [];
  const stop = f.window.__netSourceAccounting.subscribeProgress(error => { errors.push(error); });
  f.window.menuRuntimeTiming({...callback(1, 1), valid: 0});
  f.window.menuRuntimeTiming(callback(2, 1)); stop();
  const capture = await readNetSourceAccounting(f.page, {freeze: true});
  assert.equal(capture.rows.length, 1); assert.equal(capture.overflow, 1);
  assert.equal(errors.length, 2); assert(errors.every(error => /invalid source accounting/.test(error.message)));
  assert.equal(capture.errors.length, 2);
});
