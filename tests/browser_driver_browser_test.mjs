/** Real HTTP/browser harness contract test. The fixture is not gameplay evidence. */
import assert from 'node:assert/strict';
import http from 'node:http';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {loadBrowserTools} from '../scripts/browser_tools.mjs';
const {values}=parseArgs({options:{playwright:{type:'string'}}});
const {chromium,browser:launchOptions}=await loadBrowserTools(values.playwright);
const fixture=surface=>`<!doctype html><html><body>
<canvas id="canvas" tabindex="0"></canvas><div id="status">Localized startup wording</div>
${surface==='public'?'<dialog id="error-dialog"><div id="error"></div></dialog>':''}
${surface==='public'?`<button id="choose-disc" disabled>Disc</button><dialog id="disc-dialog"><input type="checkbox" id="disc-ack"><button id="disc-continue" disabled>Continue</button></dialog><input type="file" id="disc-file" hidden><button id="start-game" disabled>Play</button><button id="pause-game" disabled>Pause</button><button id="end-session" disabled>Eject</button>`:`<input id="disc" type="file" disabled><button id="launch" disabled>Play</button><button id="pause" disabled>Pause</button><button id="unload" disabled>Unload</button>`}
<script>
const $=id=>document.getElementById(id),pub=${surface==='public'};
const ids=pub?['choose-disc','start-game','pause-game','end-session','disc-file']:['disc','launch','pause','unload','disc'];
let phase=0,running=0;window.events=[];
window.Module={_melee_web_native_menu_phase:()=>phase,_melee_web_native_menu_running:()=>running};
setTimeout(()=>$(ids[0]).disabled=false,100);
$(ids[4]).onchange=()=>{if($(ids[0]).disabled)throw Error('Import before ready');$('status').dataset.runtimeError='';$(ids[1]).disabled=false;};
$(ids[1]).onclick=()=>{phase=1;running=1;$(ids[2]).disabled=$(ids[3]).disabled=false;};
$(ids[3]).onclick=()=>{if(pub){location.reload();return;}phase=0;running=0;$(ids[1]).disabled=true;};
if(pub){$('choose-disc').onclick=()=>{$('disc-ack').checked=false;$('disc-continue').disabled=true;$('disc-dialog').showModal();};$('disc-ack').onchange=()=>{$('disc-continue').disabled=!$('disc-ack').checked;};$('disc-continue').onclick=()=>{$('disc-dialog').close();$('disc-file').click();};}
for(const type of ['keydown','keyup'])document.addEventListener(type,e=>events.push(type+':'+e.key));
</script></body></html>`;
const server=http.createServer((req,res)=>{res.setHeader('content-type','text/html');res.end(fixture(req.url==='/public'?'public':'development'));});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch({...launchOptions,headless:true});
  for(const surface of ['development','public']) {
    const page=await browser.newPage();
    const driver=createBrowserDriver(page,{surface,timeoutMs:3000});
    try {
      await page.goto(`http://127.0.0.1:${server.address().port}/${surface}`);
      await driver.selectDisc({name:'fixture.iso',mimeType:'application/octet-stream',buffer:Buffer.from('harness fixture')});
      await driver.waitForStart();await driver.launch();
      await Promise.all([driver.pressChord(['a','b'],{holdMs:5,releaseMs:0}),driver.pressChord(['c'],{holdMs:0,releaseMs:0})]);
      assert.deepEqual(await page.evaluate(()=>events),['keydown:a','keydown:b','keyup:b','keyup:a','keydown:c','keyup:c']);
      await driver.unload();
      assert.equal(await page.evaluate(()=>Module._melee_web_native_menu_phase()),0);
      await page.evaluate(surface=>{
        if(surface==='public'){
          document.querySelector('#error').textContent='Preparation rejected fixture';
          document.querySelector('#error-dialog').showModal();
        }else document.querySelector('#status').dataset.runtimeError='Preparation rejected fixture';
      },surface);
      await assert.rejects(driver.waitForStart(),error=>error.step==='wait-for-start'&&error.diagnostics.error==='Preparation rejected fixture');
      await assert.rejects(driver.waitForPhase(1),/Preparation rejected fixture/);
      if(surface==='development'){
        await driver.selectDisc({name:'replacement.iso',mimeType:'application/octet-stream',buffer:Buffer.from('replacement')});
        await driver.waitForStart();
        assert.equal((await driver.diagnostics()).error,null);
      }
    } finally {driver.dispose();await page.close();}
  }
  console.log('Real-browser driver contract passed: readiness, public acknowledgement, serialized input, unload and failure diagnostics (no gameplay claim)');
} finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
