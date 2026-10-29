import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {test} from 'node:test';
import {monotonicNowNs,elapsedMs} from './stadium_c3_timing.mjs';
const source=await fs.readFile(new URL('./stadium_c1a_browser_test.mjs',import.meta.url),'utf8');
const first=source.indexOf('async function waitForBrowserPidAbsent(');
const last=source.indexOf('async function cleanupServer()',first);
assert(first>=0&&last>first);
assert(!source.slice(first,last).includes('Date.now'));
assert(source.slice(first,last).includes('timeoutMs - elapsedMs(startedNs)')); 
const actual=source.slice(first,last);
function helper(kill){return vm.runInNewContext(actual+'\nwaitForBrowserPidAbsent',{
  process:{kill},monotonicNowNs,elapsedMs,setTimeout,Number,Error,Promise});}
test('exact helper observes only signal zero; present PID later disappears',async()=>{
  const calls=[];let count=0;
  const read=helper((pid,signal)=>{calls.push([pid,signal]);if(++count===2)throw Object.assign(Error('absent'),{code:'ESRCH'});});
  assert.equal(await read(12345,100),true);
  assert.deepEqual(calls,[[12345,0],[12345,0]]);
});
test('live or inaccessible PID never becomes a successful cleanup claim',async()=>{
  const calls=[];const live=helper((pid,signal)=>calls.push([pid,signal]));
  assert.equal(await live(12345,30),false);assert(calls.length>=2);assert(calls.every(([pid,signal])=>pid===12345&&signal===0));
  const denied=helper(()=>{throw Object.assign(Error('permission'),{code:'EPERM'});});
  await assert.rejects(denied(12345,30),/permission/);
});
test('unknown PID and exhausted bound refuse without any process access',async()=>{
  let calls=0;const read=helper(()=>{calls++;});
  for(const [pid,bound] of [[null,30],[0,30],[-1,30],[1.5,30],[12345,0],[12345,NaN]])
    await assert.rejects(read(pid,bound),/exact CDP PID/);
  assert.equal(calls,0);
});
test('source preserves real CDP ownership and close-before-absence order',()=>{
  assert(!source.includes('chromium.launchServer('));assert(!source.includes('chromium.connect('));
  assert(source.includes('tools.chromium.launch(launchConfig)'));
  assert(source.includes("cdpProcesses.processInfo.filter(row => row.type === 'browser')"));
  const close=source.indexOf("await timeout(browser.close(), cleanupRemaining(), 'Browser connection close')");
  const absence=source.indexOf('browser_process_terminated = await waitForBrowserPidAbsent(');
  assert(close>=0&&absence>close);
  assert(source.includes('!report.cleanup.browser_process_terminated'));
});
