/** Real public player UI with authored Mayflash input; no gameplay claim. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';
import {mayflashMacPad} from './controller-fixtures.mjs';
const {values}=parseArgs({options:Object.fromEntries(['url','playwright','out'].map(n=>[n,{type:'string'}]))});
if(!values.url||!values.playwright||!values.out)throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out LOCAL_DIR');
const {chromium}=await import(pathToFileURL(path.join(path.resolve(values.playwright),'index.mjs')));
const browser=await chromium.launch({channel:'chrome',headless:false});
const page=await browser.newPage({viewport:{width:1100,height:800}}),errors=[];
page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(10000);
const pad=mayflashMacPad();pad.axes[3]=32*2/255-1;pad.axes[4]=34*2/255-1;
await page.addInitScript(p=>{
  window.testPad=p;
  Object.defineProperty(navigator,'platform',{value:'MacIntel'});
  Object.defineProperty(navigator,'getGamepads',{value:()=>[window.testPad]});
},pad);
const ready=()=>page.locator('#choose-disc:not([disabled])').waitFor({timeout:60000});
const rows=()=>page.evaluate(()=>Module.meleeControllers.inspect());
const choose=(port,source)=>page.getByLabel(`Player ${port} input source`,{exact:true}).selectOption(source);
const waitSource=(port,mode)=>page.waitForFunction(({port,mode})=>Module.meleeControllers.getPortSource(port)===mode,{port,mode});
try{
  await fs.mkdir(values.out,{recursive:true});await page.goto(values.url);await ready();await page.locator('canvas').focus();
  assert(await page.locator('#controls-dialog').isHidden());
  assert.equal(await page.locator('[data-controller-panel]').count(),0,'do not mount the detailed setup UI during normal play');
  assert.equal((await rows())[0].active,true,'nonzero Mayflash trigger rest still auto-activates');
  await page.evaluate(()=>{window.testPad.buttons[0]={pressed:true,value:1};});
  assert.equal((await rows())[0].output.buttons,1024);await page.evaluate(()=>{window.testPad.buttons[0]={pressed:false,value:0};});
  await page.locator('#controls-open').click();
  assert.equal(await page.getByLabel('Player 1 input source',{exact:true}).inputValue(),'auto');
  assert.equal(await page.locator('[data-controller-panel]').count(),0);
  await choose(1,'keyboard');await waitSource(0,'keyboard');
  assert.equal((await rows())[0].port,1,'P1 keyboard routes the automatic controller to P2');
  await choose(2,'keyboard');await waitSource(1,'keyboard');
  assert.equal((await rows())[0].port,-1,'both players can choose keyboard without a hidden third controller');
  await choose(1,'controller');await waitSource(0,'controller');
  assert.equal((await rows())[0].port,0,'P2 keyboard with P1 controller');
  await page.locator('#controls-close').click();await page.waitForFunction(()=>document.activeElement.id==='canvas');
  assert.equal((await rows())[0].active,true,'Done restores physical input');
  await page.reload();await ready();
  assert.equal(await page.getByLabel('Player 1 input source',{exact:true}).inputValue(),'controller');
  assert.equal(await page.getByLabel('Player 2 input source',{exact:true}).inputValue(),'keyboard');
  assert.equal(await page.locator('[data-controller-panel]').count(),0);
  await page.locator('#controls-open').click();await page.screenshot({path:path.join(values.out,'player-input-settings.png'),fullPage:true});
  await page.locator('#controls-close').click();await page.screenshot({path:path.join(values.out,'player-ready-for-disc.png'),fullPage:true});
  await page.evaluate(()=>localStorage.setItem('melee-prototype-keyboard-v1',JSON.stringify({layout:'two',one:false,two:true})));
  await page.reload();await ready();
  assert.equal(await page.getByLabel('Player 1 input source',{exact:true}).inputValue(),'controller','old disabled-keyboard preference must preserve controller use');
  assert.equal(await page.getByLabel('Player 2 input source',{exact:true}).inputValue(),'auto');
  assert.deepEqual(errors,[]);
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify({scope:'Public player default controller activation, compact settings, both mixed player directions, keyboard for both, persisted choice and restored input; authored Gamepad samples, no disc/gameplay acceptance',result:'pass',errors},null,2));
  console.log('Public player auto-controller, per-player keyboard override, compact settings, persistence and Done pass.');
}catch(error){
  await fs.writeFile(path.join(values.out,'failure.txt'),String(error)+'\n'+await page.locator('body').innerText());
  await page.screenshot({path:path.join(values.out,'failure.png'),fullPage:true});throw error;
}finally{await browser.close();}
