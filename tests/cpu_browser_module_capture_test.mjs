// The capture must observe the real published Module object before a loader
// snapshots print functions, while preserving the callbacks and their output.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {installCpuBrowserModuleCapture} from '../scripts/cpu_browser_module_capture.mjs';

const harnessSource = fs.readFileSync(new URL('../scripts/capture_cpu_browser.mjs', import.meta.url), 'utf8');
assert.doesNotMatch(harnessSource, /typeof retailRun/,
  'Failure capture must not depend on runtime-development.mjs private lexical state');
assert.match(harnessSource, /__meleeCpuModuleOutputCapture/);

const scope = {console};
scope.globalThis = scope;
vm.createContext(scope);
vm.runInContext(`(${installCpuBrowserModuleCapture.toString()})()`, scope);

const coreOutput = [], timerOutput = [];
const module = {
  print(value) { coreOutput.push([this === module, value]); return 'core-return'; },
  printErr(value) { timerOutput.push([this === module, value]); return 'timer-return'; },
};
scope.Module = module;

// Model the generated loader taking its callbacks immediately after Module is
// published. Calls through those captured function references must still be
// retained by the page-init observer.
const loaderPrint = scope.Module.print;
const loaderPrintErr = scope.Module.printErr;
assert.equal(loaderPrint('{"record":"core"}'), 'core-return');
assert.equal(loaderPrintErr('TIMER_AUDIT {"record":"timer"}'), 'timer-return');
loaderPrint('ordinary output');
loaderPrintErr('ordinary error');

const capture = scope.__meleeCpuModuleOutputCapture;
assert.deepEqual(Array.from(capture.core), ['{"record":"core"}']);
assert.deepEqual(Array.from(capture.timer), ['{"record":"timer"}']);
assert.deepEqual(coreOutput.map(([, value]) => value), ['{"record":"core"}', 'ordinary output']);
assert.deepEqual(timerOutput.map(([, value]) => value), ['TIMER_AUDIT {"record":"timer"}', 'ordinary error']);
assert.equal(capture.wrapped_modules, 1);

// Re-publishing the same Module must not add a second wrapper or duplicate a
// row, while a new module is independently wrapped once.
scope.Module = module;
scope.Module.print('{"record":"again"}');
assert.equal(coreOutput.at(-1)[0], true, 'Method calls preserve the original receiver');
const secondModule = {print(value) { coreOutput.push([this === secondModule, value]); }};
scope.Module = secondModule;
const secondLoaderPrint = scope.Module.print;
secondLoaderPrint('{"record":"second-module"}');
assert.deepEqual(Array.from(capture.core.slice(-2)), ['{"record":"again"}', '{"record":"second-module"}']);
assert.equal(capture.wrapped_modules, 2);

// Bounds are hard: rows stop at the declared limit while the original
// callback continues to receive every line and the overflow is explicit.
for (let index = capture.core.length; index < capture.core_limit; index++)
  secondLoaderPrint('{"record":"bounded"}');
secondLoaderPrint('{"record":"overflow"}');
assert.equal(capture.core.length, capture.core_limit);
assert.equal(capture.core_overflow, 1);
assert.equal(capture.core.at(-1), '{"record":"bounded"}');
assert.equal(coreOutput.at(-1)[1], '{"record":"overflow"}',
  'The second module callback remains the original behavior after capture fills');

for (let index = capture.timer.length; index < capture.timer_limit; index++)
  loaderPrintErr('TIMER_AUDIT {"record":"bounded"}');
loaderPrintErr('TIMER_AUDIT {"record":"overflow"}');
assert.equal(capture.timer.length, capture.timer_limit);
assert.equal(capture.timer_overflow, 1);
assert.equal(timerOutput.at(-1)[1], 'TIMER_AUDIT {"record":"overflow"}');

const failure = new Error('original print failure');
scope.Module = {print() { throw failure; }};
assert.throws(() => scope.Module.print('ordinary output'), error => error === failure,
  'The observer must preserve errors thrown by the original callback');

console.log('CPU browser Module print capture preserves loader callbacks, deduplicates modules and retains bounded overflow evidence.');
