import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
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
