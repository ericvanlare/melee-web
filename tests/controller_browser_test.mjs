/** Real Wasm/Aurora PAD boundary with authored browser Gamepad inputs.
 * No physical hardware, disc, graphics or gameplay acceptance is implied. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';
import {standardPad, rawPad, rawProfile, mayflashMacPad} from './controller-fixtures.mjs';
const {values} = parseArgs({options: Object.fromEntries(['url','playwright','out'].map(n=>[n,{type:'string'}]))});
if (!values.url || !values.playwright || !values.out) throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out LOCAL_DIR');
const {chromium} = await import(pathToFileURL(path.join(path.resolve(values.playwright),'index.mjs')));
const browser = await chromium.launch({channel:'chrome',headless:true});
const page = await browser.newPage();
const errors = [], checks = [], started = performance.now();
page.on('pageerror', e => errors.push(e.message));
await page.addInitScript(() => {
  window.testControllers = [];
  Object.defineProperty(navigator, 'platform', {value:'MacIntel'});
  Object.defineProperty(navigator, 'getGamepads', {value:()=>window.testControllers});
});
let pad = standardPad(3);
const set = (i,v=1) => {pad.buttons[i]={pressed:v>0.5,value:v};};
const sample = async (pads=[pad]) => page.evaluate(pads => {
  window.testControllers = pads;
  return JSON.parse(Module.UTF8ToString(Module._controller_probe_sample()));
},pads);
const check = async (name, fn) => {await fn();checks.push(name);console.log(name);};
try {
  await page.goto(new URL('controller-probe.html', values.url).href);
  await page.waitForFunction(()=>window.controllerProbeReady);
  await page.locator('canvas').focus();
  await sample();
  await check('All standard buttons reach original PAD bits through real Wasm',async()=>{
    for (const [index,bits] of [[0,256],[1,512],[2,1024],[3,2048],[5,16],[9,4096],[12,8],[13,4],[14,1],[15,2]]) {
      set(index);const state=await sample();assert.equal(state.physical_mask,1);assert.equal(state.pads[0].buttons,bits,`button ${index}`);
      set(index,0);assert.equal((await sample()).pads[0].buttons,0);
    }
  });
  await check('Independent sticks, pressure and full-squeeze trigger clicks',async()=>{
    pad.axes=[0.5,-0.5,-0.25,0.25];set(6,0.5);
    const {pads:[p]}=await sample();assert.deepEqual(p.stick,[64,64]);assert.deepEqual(p.cstick,[-32,-32]);
    assert.deepEqual(p.triggers,[128,0]);assert.equal(p.buttons,0);
    set(6);set(7);assert.equal((await sample()).pads[0].buttons,96);
    pad=standardPad(3);await sample();
  });
  await check('Focus/visibility suppress held buttons and triggers until release',async()=>{
    set(0);set(6);await sample();
    await page.evaluate(()=>Module._melee_web_input_set_activity(0,0));
    assert.equal((await sample()).pads[0].buttons,0);
    await page.evaluate(()=>Module._melee_web_input_set_activity(1,1));
    let p=(await sample()).pads[0];assert.equal(p.buttons,0);assert.equal(p.triggers[0],0);
    set(0,0);set(6,0);await sample();set(0);assert.equal((await sample()).pads[0].buttons,256);
    set(0,0);await sample();
  });
  await check('Setup gates unknown raw devices; physical X remains X after mapping',async()=>{
    pad=rawPad();await sample();set(0);assert.equal((await sample()).pads[0].buttons,0);
    set(0,0);await sample();
    await page.evaluate(profile=>{const m=Module.meleeControllers;m.saveProfile(m.inspect()[0].key,profile);},rawProfile);
    await sample();
    for(const [index,bits] of [[0,1024],[1,256],[2,512],[3,2048],[7,16],[9,4096],[4,64],[5,32]]) {
      set(index);assert.equal((await sample()).pads[0].buttons,bits);set(index,0);await sample();
    }
    pad.axes[3]=0;set(4);let p=(await sample()).pads[0];assert.equal(p.buttons,64);assert.deepEqual(p.triggers,[128,0]);
    set(4,0);pad.axes[3]=-1;
    for(const [d,bits] of [[0,8],[1,10],[2,2],[3,6],[4,4],[5,5],[6,1],[7,9]]) {
      pad.axes[6]=-1+d*2/7;assert.equal((await sample()).pads[0].buttons,bits);
    }
    pad.axes[6]=3.2857142857;pad.axes[0]=0.625;p=(await sample()).pads[0];
    assert.equal(p.buttons,0);assert.deepEqual(p.stick,[80,0]);assert.deepEqual(p.clamped.stick,[41,0]); // Existing Aurora diagnostic scales (80-15)*72/(127-15).
    pad.axes[0]=0;await sample();
  });
  await check('Settings mute live play, persist by device layout and require release',async()=>{
    await page.evaluate(()=>Module.meleeControllers.setTesting(true));set(0);
    assert.equal((await sample()).pads[0].buttons,0);
    await page.evaluate(()=>Module.meleeControllers.setTesting(false));assert.equal((await sample()).pads[0].buttons,0);
    set(0,0);await sample();set(0);assert.equal((await sample()).pads[0].buttons,1024);set(0,0);
    await page.reload();await page.waitForFunction(()=>window.controllerProbeReady);await sample();set(0);
    assert.equal((await sample()).pads[0].buttons,1024);set(0,0);await sample();
  });
  await check('Four ports, reassignment, disconnect and reused browser index',async()=>{
    const pads=[pad,standardPad(4),standardPad(8),standardPad(11)];await sample(pads);
    pads[3].buttons[1]={pressed:true,value:1};let state=await sample(pads);
    assert.equal(state.physical_mask,15);assert.equal(state.pads[3].buttons,512);
    pads[3].buttons[1]={pressed:false,value:0};await sample(pads);
    await page.evaluate(()=>{const m=Module.meleeControllers;m.assign(m.inspect()[3].key,0);});await sample(pads);
    pads[3].buttons[0]={pressed:true,value:1};assert.equal((await sample(pads)).pads[0].buttons,256);
    assert.equal((await sample([])).physical_mask,0);
    pad=standardPad(0,'Replacement standard device');await sample();set(0);assert.equal((await sample()).pads[0].buttons,256);set(0,0);
  });
  await check('Physical input retains per-port priority over SDL keyboard',async()=>{
    await page.evaluate(()=>Module._melee_web_input_set_keyboard(1));await page.locator('canvas').focus();await sample();
    await page.keyboard.down('j');let s=await sample();assert.equal(s.keyboard_active_mask,0);assert.equal(s.pads[0].buttons,0);await page.keyboard.up('j');
    await sample([]);await page.keyboard.down('j');s=await sample([]);assert.equal(s.keyboard_active_mask,1);assert.equal(s.pads[0].buttons,256);await page.keyboard.up('j');await sample([]);
  });
  await check('Recognized Mayflash suggestion reaches Wasm without setup',async()=>{
    pad=mayflashMacPad();await sample();set(0);
    assert.equal((await sample()).pads[0].buttons,1024);set(0,0);pad.axes[3]=0;
    let p=(await sample()).pads[0];assert.equal(p.buttons,0);assert.deepEqual(p.triggers,[128,0]);
    set(4);p=(await sample()).pads[0];assert.equal(p.buttons,64);assert.equal(p.triggers[0],128);
    set(4,0);pad.axes[3]=-1;pad.axes[9]=-1+2*2/7;
    assert.equal((await sample()).pads[0].buttons,2);
  });
  assert.deepEqual(errors,[]);
} finally {
  await fs.mkdir(values.out,{recursive:true});
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify({scope:'Authored Gamepad → browser mapper → real Wasm/Aurora PAD; no physical or gameplay claim',browser:browser.version(),checks,errors,seconds:(performance.now()-started)/1000},null,2));
  await browser.close();
}
