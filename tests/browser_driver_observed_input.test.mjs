import assert from 'node:assert/strict';
import test from 'node:test';
import {EventEmitter} from 'node:events';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {observeDesiredSourcePad} from '../scripts/a3_local_room_full_route.mjs';

const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
// Component-only source polling fixture. No native input or browser evidence.
class Page extends EventEmitter {
  constructor({sampleAfter=100,predicateError=false}={}) {
    super();this.events=[];this.held=new Set();this.captures=[];
    this.waitDurations=[];this.sampleAfter=sampleAfter;this.predicateError=predicateError;this.pendingObservers=0;
    this.keyboard={down:async key=>{
      this.events.push('down:'+key);this.held.add(key);
      if(!this.sourceTimer)this.sourceTimer=setTimeout(()=>{
        this.captures.push({source_cursor:10,input_tick:10,local_port:0,poll_serial:1,
          bytes:[this.held.has('Delete')?4:this.held.has('j')?1:0,0,this.held.has('d')?127:0,0,0,0,0,0,0,0,0]});
      },sampleAfter);
    },up:async key=>{this.events.push('up:'+key);this.held.delete(key);}};
  }
  locator(){return {focus:async()=>this.events.push('focus')};}
  async waitForTimeout(ms){this.waitDurations.push(ms);await pause(ms);}
  async evaluate(){return {phase:3,running:1};}
  async waitForFunction(predicate,arg,{timeout,polling}) {
    ++this.pendingObservers;this.events.push('observer-start');
    const deadline=Date.now()+timeout;
    try {
      while(Date.now()<deadline) {
        const previous=globalThis.meleeNetRuntimeLockstepHealth;
        globalThis.meleeNetRuntimeLockstepHealth=()=>({failure:this.predicateError?'owner failed':null,
          peer:{localInputCapture:{captures_since:this.captures},protocol:{terminal:null}}});
        let result;
        try {result=predicate(arg);}finally{globalThis.meleeNetRuntimeLockstepHealth=previous;}
        if(result)return {jsonValue:async()=>result,dispose:async()=>this.events.push('handle-disposed')};
        await pause(Math.min(polling,Math.max(1,deadline-Date.now())));
      }
      throw Error('source observation timeout');
    } finally {--this.pendingObservers;this.events.push('observer-joined');}
  }
  dispose(){clearTimeout(this.sourceTimer);}
}
const observed={predicate:observeDesiredSourcePad,arg:{inputFrom:10,port:0,expected:{x:1}},timeoutMs:300};

test('fixed40ms press can miss a100ms source polling gap',async()=>{
  const page=new Page(),driver=createBrowserDriver(page,{timeoutMs:1000});
  try {await driver.pressChord(['d'],{holdMs:40,releaseMs:0});await pause(80);
    assert.equal(page.captures.length,1);assert.equal(page.captures[0].bytes[2],0);
    assert.equal(page.pendingObservers,0);assert.equal(page.held.size,0);
  } finally {driver.dispose();page.dispose();}
});

test('observed press holds to first actual desired readback then disposes observer before release',async()=>{
  const page=new Page(),driver=createBrowserDriver(page,{timeoutMs:1000});
  try {const result=await driver.pressChord(['d'],{holdMs:40,releaseMs:0,observeBeforeRelease:observed});
    assert.deepEqual(result.bytes,[0,0,127,0,0,0,0,0,0,0,0]);assert.equal(result.source_cursor,result.input_tick);
    assert(page.events.indexOf('observer-joined')<page.events.indexOf('up:d'));
    assert(page.events.indexOf('handle-disposed')<page.events.indexOf('up:d'));
    assert.equal(page.pendingObservers,0);assert.equal(page.held.size,0);
  } finally {driver.dispose();page.dispose();}
});

test('observation timeout releases every chord key and serialized queued input fails closed',async()=>{
  const page=new Page({sampleAfter:400}),driver=createBrowserDriver(page,{timeoutMs:1000});
  try {const result=await Promise.allSettled([
    driver.pressChord(['d','w'],{holdMs:40,releaseMs:0,observeBeforeRelease:{...observed,timeoutMs:70}}),
    driver.pressChord(['a'],{holdMs:0,releaseMs:0})]);
    assert(result.every(row=>row.status==='rejected'));
    assert.deepEqual(page.events.filter(x=>x.startsWith('up:')),['up:w','up:d']);
    assert(!page.events.includes('down:a'));assert.equal(page.held.size,0);assert.equal(page.pendingObservers,0);
  } finally {driver.dispose();page.dispose();}
});

test('predicate error joins observation and releases all keys',async()=>{
  const page=new Page({predicateError:true}),driver=createBrowserDriver(page,{timeoutMs:1000});
  try {await assert.rejects(driver.pressChord(['d','w'],{releaseMs:0,observeBeforeRelease:observed}),/owner failure/);
    assert.equal(page.held.size,0);assert.equal(page.pendingObservers,0);
    assert(page.events.indexOf('observer-joined')<page.events.indexOf('up:w'));
  } finally {driver.dispose();page.dispose();}
});

test('observation cap above500 or beyond remaining budget refuses before any keydown',async()=>{
  for(const options of [{timeoutMs:1000,cap:501},{timeoutMs:100,cap:100}]) {
    const page=new Page(),driver=createBrowserDriver(page,{timeoutMs:options.timeoutMs});
    try {await assert.rejects(driver.pressChord(['d'],{releaseMs:10,
      observeBeforeRelease:{...observed,timeoutMs:options.cap}}),/observation|wall-time/);
      assert.deepEqual(page.events,[]);
    } finally {driver.dispose();page.dispose();}
  }
});

test('actual route predicate refuses stale, malformed, other-port and nonaligned samples',()=>{
  const base={input_tick:10,source_cursor:10,local_port:0,bytes:[0,0,127,0,0,0,0,0,0,0,0]};
  const previous=globalThis.meleeNetRuntimeLockstepHealth;
  try {
    for(const bad of [{...base,input_tick:9,source_cursor:9},{...base,source_cursor:11},
      {...base,local_port:1},{...base,bytes:[0,0,127]}, {...base,bytes:[0,0,999,0,0,0,0,0,0,0,0]}]) {
      globalThis.meleeNetRuntimeLockstepHealth=()=>({peer:{localInputCapture:{captures_since:[bad]}}});
      assert.equal(observeDesiredSourcePad(observed.arg),false);
    }
    globalThis.meleeNetRuntimeLockstepHealth=()=>({peer:{protocol:{terminal:{kind:'disconnect'}}}});
    assert.throws(()=>observeDesiredSourcePad(observed.arg),/terminal/);
  }finally{globalThis.meleeNetRuntimeLockstepHealth=previous;}
});


test('P2 jump desired-X observer releases after first captured button rather than fixed full-hop hold',async()=>{
  const page=new Page(),driver=createBrowserDriver(page,{timeoutMs:1000});
  try {
    const result=await driver.pressChord(['Delete'],{holdMs:167,releaseMs:0,
      observeBeforeRelease:{...observed,arg:{inputFrom:10,port:0,expected:{button:0x0400}}}});
    assert.deepEqual(result.bytes,[4,0,0,0,0,0,0,0,0,0,0]);
    assert.equal(result.input_tick,10);
    assert(!page.waitDurations.includes(167));
    assert.equal(page.held.size,0);assert.equal(page.pendingObservers,0);
    assert(page.events.indexOf('handle-disposed')<page.events.indexOf('up:Delete'));
  }finally{driver.dispose();page.dispose();}
});

test('P1 jab default remains fixed hold and literal A sample',async()=>{
  const page=new Page(),driver=createBrowserDriver(page,{timeoutMs:1000});
  try {
    await driver.pressChord(['j'],{holdMs:167,releaseMs:0});
    assert.deepEqual(page.captures[0].bytes,[1,0,0,0,0,0,0,0,0,0,0]);
    assert(page.waitDurations.includes(167));assert(!page.events.includes('observer-start'));
    assert.equal(page.held.size,0);
  }finally{driver.dispose();page.dispose();}
});
