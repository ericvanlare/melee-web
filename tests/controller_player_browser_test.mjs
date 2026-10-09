/** Shared controls in the real public/development players; authored input, no gameplay claim. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {configureSparseControllerSettings,readSparseControllerReady} from './sparse_controller_settings_driver.mjs';
import {mayflashMacPad, standardPad} from './controller-fixtures.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
const {values}=parseArgs({options:{
  ...Object.fromEntries(['url','playwright','out'].map(n=>[n,{type:'string'}])),
  headed:{type:'boolean',default:false},
  'sparse-controls-only':{type:'boolean',default:false},
}});
if(!values.url||!values.playwright||!values.out)throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out LOCAL_DIR [--headed]');
const {chromium,browser:launchOptions}=await loadBrowserTools(values.playwright);
const browser=await chromium.launch(browserLaunchOptions(launchOptions,{headed:values.headed}));
const page=await browser.newPage({viewport:{width:1100,height:800}}),errors=[];
page.on('pageerror',e=>errors.push(e.message));page.setDefaultTimeout(10000);
const pad=values['sparse-controls-only']?standardPad(0,'Sparse Mario authored standard Gamepad'):mayflashMacPad();
if(!values['sparse-controls-only']){pad.axes[3]=32*2/255-1;pad.axes[4]=34*2/255-1;}
await page.addInitScript(p=>{
  window.testPad=p;
  Object.defineProperty(navigator,'platform',{value:'MacIntel'});
  Object.defineProperty(navigator,'getGamepads',{value:()=>window.testPads||[window.testPad]});
},pad);
const development=new URL(values.url).pathname.endsWith('/runtime.html');
const ready=()=>page.locator(development?'#disc:not([disabled])':'#choose-disc:not([disabled])').waitFor({timeout:60000});
const rows=()=>page.evaluate(()=>Module.meleeControllers.inspect());
const choose=(port,source)=>page.getByLabel(`Player ${port} input source`,{exact:true}).selectOption(source);
const waitSource=(port,mode)=>page.waitForFunction(({port,mode})=>Module.meleeControllers.getPortSource(port)===mode,{port,mode});
const reducedReport={scope:'Before-disc actual Controls sparse virtual-device assignment and deliberate absent-selector cleanup only; no disc/gameplay/physical-input claim',result:'pending',checks:{},cleanup:{}};
let primaryError,closing;
function closeReducedResources(){
 return closing??=(async()=>{
  for(const [name,resource] of [['context',page.context()],['browser',browser]]){
   try{await resource.close();reducedReport.cleanup[name]='closed';}
   catch(error){reducedReport.cleanup[name]=String(error);reducedReport.result='fail';if(!primaryError)primaryError=error;}
  }
 })();
}
const onReducedSignal=()=>{primaryError??=Error('Reduced Controls interrupted by owned supervisor');
 reducedReport.result='fail';void closeReducedResources();};
if(values['sparse-controls-only'])for(const signal of ['SIGTERM','SIGINT'])process.once(signal,onReducedSignal);
try{
 route:{
  await fs.mkdir(values.out,{recursive:true});await page.goto(values.url);await ready();await page.locator('canvas').focus();
  if(values['sparse-controls-only']){
   assert(development,'Reduced sparse Controls requires the development player');
   reducedReport.checks.assignment={};
   await configureSparseControllerSettings({page,retention:reducedReport.checks.assignment});
   await page.screenshot({path:path.join(values.out,'sparse-controls-assigned.png'),fullPage:true});
   reducedReport.checks.deliberateFailure={};
   await assert.rejects(configureSparseControllerSettings({page,retention:reducedReport.checks.deliberateFailure,
     deviceLabel:'Player port for deliberately absent reducer device',selectorTimeout:1000}),/Timeout/);
   assert.equal(reducedReport.checks.deliberateFailure.dialogClosed,true);
   await page.waitForFunction(readSparseControllerReady,undefined,{timeout:10000});
   reducedReport.checks.afterFailureRows=await rows();
   assert.deepEqual(errors,[]);reducedReport.result='pass';break route;
  }
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
  assert.equal((await rows())[0].port,development?2:-1,
    development?'P1/P2 keyboard preserves the development runtime’s other controller ports':'both players can choose keyboard without a hidden third controller');
  await choose(1,'controller');await waitSource(0,'controller');
  assert.equal((await rows())[0].port,0,'P2 keyboard with P1 controller');
  await page.locator('#controller-advanced summary').click();
  await page.locator('[data-controller-panel]').waitFor();
  await page.locator('#controller-advanced summary').click();
  await page.locator('[data-controller-panel]').waitFor({state:'detached'});
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
  await page.evaluate(()=>localStorage.setItem('melee-prototype-keyboard-v1',JSON.stringify({layout:'boxx',sources:['auto','keyboard']})));
  await page.reload();await ready();
  assert.equal(await page.getByLabel('Player 2 input source',{exact:true}).inputValue(),'auto','restored B0XX settings cannot advertise an unavailable P2 keyboard');
  await page.locator('#controls-open').click();
  await page.locator('#keyboard-layout').selectOption('two');
  await page.locator('#controls-close').click();
  if(development){
    await page.locator('#controls-open').click();
    await choose(1,'keyboard');await waitSource(0,'keyboard');
    const saved=await page.evaluate(()=>localStorage.getItem('melee-prototype-keyboard-v1'));
    // The prototype iframe's legacy checkbox remains a session-only adapter.
    await page.evaluate(()=>{const input=document.querySelector('#keyboard');input.checked=false;input.dispatchEvent(new Event('change'));});
    await waitSource(0,'controller');
    await page.evaluate(()=>{const input=document.querySelector('#keyboard');input.checked=true;input.dispatchEvent(new Event('change'));});
    await waitSource(0,'auto');
    assert.equal(await page.evaluate(()=>localStorage.getItem('melee-prototype-keyboard-v1')),saved,'legacy session overrides must not overwrite saved settings');
    await page.locator('#controls-close').click();
  }
  await page.locator('#controls-open').click();
  await page.locator('#keyboard-layout').selectOption('boxx');
  await page.waitForFunction(()=>document.querySelector('#player-two-source option[value=keyboard]').disabled);
  assert(await page.locator('#boxx-source-note').isVisible());
  await page.locator('#controls-close').click();
  await page.waitForFunction(()=>document.activeElement.id==='canvas');
  // Four browser-exposed devices use four distinct ports through the same manager.
  // These are authored Gamepad API samples, not a physical-controller capture.
  await page.locator('#controls-open').click();
  for(const port of [3,4]){
    assert.equal(await page.getByLabel(`Player ${port} input source`,{exact:true}).inputValue(),development?'auto':'off');
    assert.equal(await page.locator(`#player-${port===3?'three':'four'}-source option[value=keyboard]`).count(),0);
    assert.equal(await page.locator(`#player-${port===3?'three':'four'}-source option[value=touch]`).count(),0);
  }
  const pads=Array.from({length:4},(_,index)=>standardPad(index));
  await page.evaluate(p=>{window.testPads=p;},pads);
  for(let port=1;port<=4;port++){await choose(port,'controller');await waitSource(port-1,'controller');}
  await page.locator('#controls-close').click();
  await page.waitForFunction(()=>{
    const rows=Module.meleeControllers.inspect();
    return rows.length===4&&rows.every(row=>row.active&&row.port===row.index);
  });
  await page.evaluate(()=>window.testPads.forEach((pad,index)=>{pad.buttons[index]={pressed:true,value:1};}));
  await page.waitForFunction(()=>{
    const expected=[0x100,0x200,0x400,0x800],rows=Module.meleeControllers.inspect();
    return rows.length===4&&rows.every(row=>row.output.buttons===expected[row.port]);
  });
  assert.deepEqual((await rows()).map(row=>row.port).sort(),[0,1,2,3]);
  await page.evaluate(()=>window.testPads.forEach((pad,index)=>{pad.buttons[index]={pressed:false,value:0};}));
  await page.locator('#controls-open').click();
  await page.screenshot({path:path.join(values.out,'four-player-controller-routing.png'),fullPage:true});
  await choose(3,'off');await waitSource(2,'off');
  assert(!(await rows()).some(row=>row.port===2),'an off port has no automatic controller assignment');
  await choose(3,'controller');await waitSource(2,'controller');
  await page.locator('#controls-close').click();
  await page.waitForFunction(()=>Module.meleeControllers.inspect().every(row=>row.active&&row.port===row.index));
  await page.evaluate(()=>{window.testPads[3]=null;});
  await page.waitForFunction(()=>Module.meleeControllers.inspect().length===3);
  await page.evaluate(p=>{window.testPads[3]=p;},standardPad(3));
  await page.waitForFunction(()=>{
    const rows=Module.meleeControllers.inspect();
    return rows.length===4&&rows.every(row=>row.active&&row.port===row.index);
  });
  assert.deepEqual(await page.evaluate(()=>JSON.parse(localStorage.getItem('melee-prototype-keyboard-v1')).sources),
    ['controller','controller','controller','controller']);
  await page.reload();await ready();
  for(let port=1;port<=4;port++){
    assert.equal(await page.getByLabel(`Player ${port} input source`,{exact:true}).inputValue(),'controller');
    await waitSource(port-1,'controller');
  }
  await page.evaluate(()=>localStorage.setItem('melee-prototype-keyboard-v1',JSON.stringify({layout:'two',sources:['auto','auto','keyboard','touch']})));
  await page.reload();await ready();
  for(const port of [3,4]){
    assert.equal(await page.getByLabel(`Player ${port} input source`,{exact:true}).inputValue(),development?'auto':'off',
      'unsupported stored keyboard/touch choices retain the extra-port default');
  }
  await page.addInitScript(()=>Object.defineProperty(window,'localStorage',{get(){throw Error('Storage unavailable in this browser context');}}));
  await page.reload();await ready();
  await page.locator('#controls-open').click();
  assert.equal(await page.getByLabel('Player 1 input source',{exact:true}).inputValue(),'auto');
  await choose(1,'keyboard');await waitSource(0,'keyboard');
  await page.setViewportSize({width:320,height:700});
  const bounds=await page.locator('#controls-dialog').boundingBox();
  assert(bounds.x>=0&&bounds.x+bounds.width<=320,'shared controls stay within a narrow viewport');
  await page.locator('#controls-close').click();
  await page.waitForFunction(()=>document.activeElement.id==='canvas');
  assert.deepEqual(errors,[]);
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify({scope:`${development?'Development':'Public'} player default controller activation, shared compact settings, four independent controller ports, off and reconnect, persisted four-port choice, two-player legacy compatibility, B0XX and restored input; authored Gamepad samples, no disc/gameplay or physical-controller acceptance`,result:'pass',browser_mode:values.headed?'headed':'headless',errors},null,2));
  console.log(`${development?'Development':'Public'} player shared controller settings pass.`);
 }
}catch(error){
  primaryError=error;reducedReport.result='fail';reducedReport.failure=String(error);
  if(values['sparse-controls-only']){
   try{await fs.writeFile(path.join(values.out,'failure.txt'),String(error)+'\n'+await page.locator('body').innerText());}
   catch(diagnosticError){reducedReport.bodyError=String(diagnosticError);}
   try{await page.screenshot({path:path.join(values.out,'failure.png'),fullPage:true});}
   catch(diagnosticError){reducedReport.screenshotError=String(diagnosticError);}
  }else{
   await fs.writeFile(path.join(values.out,'failure.txt'),String(error)+'\n'+await page.locator('body').innerText());
   await page.screenshot({path:path.join(values.out,'failure.png'),fullPage:true});
  }
  throw error;
}finally{
 if(values['sparse-controls-only']){
  await closeReducedResources();
  for(const signal of ['SIGTERM','SIGINT'])process.removeListener(signal,onReducedSignal);
  try{await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify(reducedReport,null,2));}
  catch(error){if(!primaryError)primaryError=error;}
  if(primaryError)throw primaryError;
 }else await browser.close();
}
