import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import {readFile} from 'node:fs/promises';
import {installNetSourceAccounting, readNetSourceAccounting,
  verifyNetSourceAccounting} from '../scripts/net_source_accounting.mjs';

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
  const source = await readFile(new URL('../scripts/net_session_instance.mjs', import.meta.url), 'utf8');
  const helpers = source.slice(source.indexOf('const PAGE_HELPERS ='), source.indexOf('export async function openNetInstance'));
  const f = fixture(); await installNetSourceAccounting(f.page);
  const observer = f.window.menuRuntimeTiming;
  vm.runInNewContext(`${helpers}; PAGE_HELPERS()`, {window: f.window, Module: {}});
  const native = f.window.__meleeWebNetNativePeerApi();
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
