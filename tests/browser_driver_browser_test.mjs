/** Real HTTP/browser harness contract test. The fixture is not gameplay evidence. */
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {execFile} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {parseArgs, promisify} from 'node:util';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
const {values}=parseArgs({options:{playwright:{type:'string'}}});
const {chromium,browser:launchOptions,playwrightPath}=await loadBrowserTools(values.playwright);
const fixture=(surface,failUnload=false)=>`<!doctype html><html><body>
<canvas id="canvas" tabindex="0"></canvas><div id="status">Localized startup wording</div>
${surface==='public'?'<dialog id="error-dialog"><div id="error"></div></dialog>':''}
${surface==='public'?`<button id="choose-disc" disabled>Disc</button><dialog id="disc-dialog"><input type="checkbox" id="disc-ack"><button id="disc-continue" disabled>Continue</button></dialog><input type="file" id="disc-file" hidden><button id="start-game" disabled>Play</button><button id="pause-game" disabled>Pause</button><button id="end-session" disabled>Eject</button>`:`<input id="disc" type="file" disabled><button id="launch" disabled>Play</button><button id="pause" disabled>Pause</button><button id="unload" disabled>Unload</button>`}
<script>
const $=id=>document.getElementById(id),pub=${surface==='public'};
const ids=pub?['choose-disc','start-game','pause-game','end-session','disc-file']:['disc','launch','pause','unload','disc'];
let phase=0,running=0;window.events=[];window.failUnload=${failUnload};
window.Module={_melee_web_native_menu_phase:()=>phase,_melee_web_native_menu_running:()=>running};
setTimeout(()=>$(ids[0]).disabled=false,100);
$(ids[4]).onchange=()=>{if($(ids[0]).disabled)throw Error('Import before ready');$('status').dataset.runtimeError='';$(ids[1]).disabled=false;};
$(ids[1]).onclick=()=>{phase=1;running=1;$(ids[2]).disabled=$(ids[3]).disabled=false;};
$(ids[3]).onclick=()=>{if(pub){location.reload();return;}$('status').dataset.runtimeError='';phase=0;running=0;$(ids[1]).disabled=true;if(window.failUnload)$('status').dataset.runtimeError='Pending renderer work did not drain. Reload to recover.';};
if(pub){$('choose-disc').onclick=()=>{$('disc-ack').checked=false;$('disc-continue').disabled=true;$('disc-dialog').showModal();};$('disc-ack').onchange=()=>{$('disc-continue').disabled=!$('disc-ack').checked;};$('disc-continue').onclick=()=>{$('disc-dialog').close();$('disc-file').click();};}
for(const type of ['keydown','keyup'])document.addEventListener(type,e=>events.push(type+':'+e.key));
</script></body></html>`;
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost');
  res.writeHead(200,{'content-type':'text/html','Cross-Origin-Opener-Policy':'same-origin','Cross-Origin-Embedder-Policy':'require-corp'});
  res.end(fixture(url.pathname==='/public'?'public':'development',url.searchParams.has('fail-unload')));
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
let browser;
try {
  browser=await chromium.launch(browserLaunchOptions(launchOptions));
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
        await driver.launch();
        await page.evaluate(()=>{window.failUnload=true;});
        await assert.rejects(driver.unload(),error=>error.step==='unload'&&error.diagnostics.phase===0&&/did not drain/.test(error.diagnostics.error));
        await assert.rejects(driver.waitForImport(),/did not drain/);
        await page.evaluate(()=>{window.failUnload=false;});
        await driver.unload();
        assert.equal((await driver.diagnostics()).error,null);
      }
    } finally {driver.dispose();await page.close();}
  }
  const temporary=await fs.mkdtemp(path.join(os.tmpdir(),'melee-browser-smoke-'));
  try {
    const disc=path.join(temporary,'fixture.iso');await fs.writeFile(disc,'harness fixture');
    const smoke=(out,query='')=>promisify(execFile)(process.execPath,[
      fileURLToPath(new URL('../scripts/browser_smoke.mjs',import.meta.url)),
      '--url',`http://127.0.0.1:${server.address().port}/development${query}`,
      '--surface','development','--disc',disc,'--out',out,'--playwright',playwrightPath,'--timeout','10000',
    ],{timeout:30000});
    const out=path.join(temporary,'new-parent','run');
    await smoke(out);
    const original=await fs.readFile(path.join(out,'report.json'),'utf8');
    assert.equal(JSON.parse(original).result,'pass');
    await assert.rejects(smoke(out),error=>error.code===1&&/EEXIST/.test(error.stderr));
    assert.equal(await fs.readFile(path.join(out,'report.json'),'utf8'),original);
    const failed=path.join(temporary,'failed');
    await assert.rejects(smoke(failed,'?fail-unload=1'),error=>error.code===1);
    const report=JSON.parse(await fs.readFile(path.join(failed,'report.json'),'utf8'));
    assert.equal(report.result,'fail');assert.equal(report.step,'unload');
    assert.match(report.failure,/did not drain/);
    assert.equal(report.state.phase,0);
    assert(!report.checks.includes('teardown and import control ready'));
  } finally {await fs.rm(temporary,{recursive:true,force:true});}
  console.log('Real-browser driver and smoke contracts passed: readiness, input, recovery, failed teardown and exclusive output creation (no gameplay claim)');
} finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
