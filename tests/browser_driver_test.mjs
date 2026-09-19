import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';

class Page extends EventEmitter {
  constructor() {
    super();this.events=[];this.failDown=null;
    this.keyboard={down:async key=>{
      this.events.push('down:'+key);if(this.failDown===key)throw Error('lost input');
    },up:async key=>{this.events.push('up:'+key);}};
  }
  locator(){return {focus:async()=>{this.events.push('focus');}};}
  async waitForTimeout(ms){await new Promise(resolve=>setTimeout(resolve,ms));}
  async evaluate(){return {status:'translated text',phase:1,running:1};}
}
const page=new Page();const driver=createBrowserDriver(page,{timeoutMs:1000});
await Promise.all([
  driver.pressChord(['A','B'],{holdMs:5,releaseMs:0}),
  driver.pressChord(['C'],{holdMs:0,releaseMs:0}),
]);
assert.deepEqual(page.events,['focus','down:A','down:B','up:B','up:A','focus','down:C','up:C']);
driver.dispose();assert.equal(page.listenerCount('pageerror'),0);

const failure=new Page();failure.failDown='B';
const failedDriver=createBrowserDriver(failure,{timeoutMs:1000});
const attempts=await Promise.allSettled([
  failedDriver.pressChord(['A','B'],{holdMs:0,releaseMs:0}),
  failedDriver.pressChord(['C'],{holdMs:0,releaseMs:0}),
]);
assert(attempts.every(attempt=>attempt.status==='rejected'));
assert.deepEqual(failure.events,['focus','down:A','down:B','up:B','up:A']);
assert.equal(attempts[0].reason.step,'keyboard-chord');
assert.equal(attempts[0].reason.diagnostics.phase,1);
failedDriver.dispose();

const expired=new Page();
const expiredDriver=createBrowserDriver(expired,{deadline:Date.now()-1});
await assert.rejects(expiredDriver.pressChord(['A']),/wall-time bound/);
assert.deepEqual(expired.events,[]);expiredDriver.dispose();
assert.throws(()=>createBrowserDriver(new Page(),{surface:'typo'}),/Unknown/);
console.log('Browser driver input ordering, cleanup and deadline checks passed');
