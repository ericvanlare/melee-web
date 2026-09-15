/** Guided setup with authored Gamepad input: no physical-device claim. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';
import {rawPad, rawProfile} from './controller-fixtures.mjs';
const {values} = parseArgs({options:{...Object.fromEntries(['url','playwright','out'].map(n=>[n,{type:'string'}])), 'signed-dpad':{type:'boolean'}}});
if (!values.url || !values.playwright || !values.out) throw Error('Use --url ORIGIN --playwright PACKAGE_DIR --out LOCAL_DIR');
const {chromium} = await import(pathToFileURL(path.join(path.resolve(values.playwright),'index.mjs')));
const browser = await chromium.launch({channel:'chrome',headless:true});
const page = await browser.newPage({viewport:{width:1100,height:1000}}), errors=[];
page.setDefaultTimeout(8000);
page.on('pageerror',e=>errors.push(e.message));
await page.addInitScript(()=>{
  window.testControllers=[];
  Object.defineProperty(navigator,'getGamepads',{value:()=>window.testControllers});
});
const profile=structuredClone(rawProfile);
const makePad=()=>{
  const p=rawPad();
  if(values['signed-dpad']) {p.axes[6]=0;p.axes.push(0);}
  return p;
};
if(values['signed-dpad']) {
  for(const [name,index,end] of [['Up',6,-1],['Down',6,1],['Left',7,-1],['Right',7,1]]) profile.buttons[name]={kind:'axis',index,rest:0,end};
  profile.axes.triggerL={kind:'button',index:6};profile.axes.triggerR={kind:'button',index:8};
}
const raw = makePad();
const send = () => page.evaluate(p=>{window.testControllers=[p];},raw);
const dialog = page.getByRole('dialog',{name:'Controller mapping'});
const step = number => page.waitForFunction(n=>document.querySelector('.mcp-dialog .mcp-caption')?.textContent===`Step ${n} of 18`,number);
const release = async () => {Object.assign(raw,makePad());await send();};
const started=performance.now();
try {
  await page.goto(new URL('controller-check.html',values.url).href);
  await page.getByText(/press a button to let the browser detect/).waitFor();
  assert(await page.getByRole('button',{name:'Set up selected',exact:true}).isDisabled());
  await send();await page.waitForFunction(()=>[...document.querySelectorAll('button')].some(b=>b.textContent==='Set up selected'&&!b.disabled));
  assert.equal(await page.getByLabel('Controller kind',{exact:true}).inputValue(),'gamecube','recognize adapter identity for wizard kind');
  await page.getByRole('button',{name:'Start recording',exact:true}).click();
  await page.getByRole('button',{name:'Set up selected',exact:true}).click();
  await page.getByLabel('Profile name').fill('Browser wizard fixture');
  const actions=['A','B','X','Y','Z','Start','L','R','Up','Down','Left','Right',
    'stickX','stickY','cstickX','cstickY','triggerL','triggerR'];
  for(let i=0;i<actions.length;i++) {
    const action=actions[i], binding=(profile.buttons[action]||profile.axes[action]);
    await step(i+1);await release();
    await page.waitForFunction(()=>/first pressed|Hold the requested/.test(document.querySelector('.mcp-dialog .mcp-muted')?.textContent||''));
    if(binding.kind==='button') raw.buttons[binding.index]={pressed:true,value:1};
    else if(binding.kind==='hat') raw.axes[binding.index]=-1+binding.direction*2/7;
    else raw.axes[binding.index]=action.startsWith('trigger')?1:binding.end*.625;
    await send();
    if(i<actions.length-1) {
      await step(i+2);
      // Model a human holding the previous control while reading the next prompt.
      await page.waitForTimeout(350);
      assert.match(await dialog.locator('.mcp-muted').innerText(),/Release|released|center/);
      assert(!/first pressed|Hold the requested/.test(await dialog.locator('.mcp-muted').innerText()));
    }
  }
  await dialog.waitFor({state:'hidden'});await release();
  await page.getByRole('button',{name:'Stop and prepare download',exact:true}).click();
  const downloadEvent=page.waitForEvent('download');await page.getByRole('link',{name:'Download recording'}).click();
  await fs.mkdir(values.out,{recursive:true});
  const capturePath=path.join(values.out,'synthetic-wizard-capture.json');await (await downloadEvent).saveAs(capturePath);
  const capture=JSON.parse(await fs.readFile(capturePath,'utf8'));
  for(const action of actions) assert(capture.samples.some(s=>s.prompt?.action===action),`record prompt ${action}`);
  const configured=await page.evaluate(()=>JSON.parse(localStorage.getItem('melee-controller-profiles-v1')).profiles[0][1]);
  const expected=structuredClone(profile);expected.name='Browser wizard fixture';assert.deepEqual(configured,expected);
  raw.buttons[0]={pressed:true,value:1};await send();
  await page.getByLabel('X pressed',{exact:true}).waitFor();assert(await page.getByLabel('A released',{exact:true}).isVisible());
  await page.screenshot({path:path.join(values.out,'controller-panel.png'),fullPage:true});
  assert.deepEqual(errors,[]);
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify({scope:'18-control wizard with constructed raw GameCube layout, including held-control release, prompt capture, saved mapping and physical-X simulation',seconds:(performance.now()-started)/1000,errors},null,2));
  console.log('All 18 wizard steps, held controls, local recording and saved GameCube profile pass.');
} catch (error) {
  await fs.mkdir(values.out,{recursive:true});
  await fs.writeFile(path.join(values.out,'failure.txt'),String(error)+'\n'+await page.locator('body').innerText());
  await page.screenshot({path:path.join(values.out,'failure.png'),fullPage:true});
  throw error;
} finally {await browser.close();}
