#!/usr/bin/env node
/** Real-HTTP interface check; optional owned-disc smoke. Never an admission run. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';
const {values} = parseArgs({options:Object.fromEntries(['url','playwright','disc','out'].map(name => [name,{type:'string'}]))});
if (!values.url || !values.out) throw Error('Use --url http://127.0.0.1:PORT/prototype.html --out work/prototype-browser [--disc PATH] [--playwright PACKAGE_DIR]');
const {chromium} = values.playwright ? await import(pathToFileURL(path.join(path.resolve(values.playwright),'index.mjs')).href) : await import('playwright');
await fs.mkdir(values.out,{recursive:true});
const browser = await chromium.launch({channel:'chrome',headless:false,chromiumSandbox:true});
const failures=[], errors=[], posts=[], checks=[];
let failureState = null;
let timingResumes = 0;
const page = await browser.newPage({viewport:{width:1440,height:1050},deviceScaleFactor:1});
// This suite drives keyboard input. Physical auto-assignment has a separate
// shared-controls regression and must not replace the keyboard on this host.
await page.addInitScript(() => Object.defineProperty(navigator, 'getGamepads', {value: () => []}));
page.on('pageerror', error=>errors.push(error.message));
page.on('console', message=>{if(message.type()==='error')errors.push(message.text())});
page.on('response', response=>{if(response.status()>=400)errors.push(`${response.status()} ${new URL(response.url()).pathname}`)});
page.on('request', request=>{if(request.method()!=='GET')posts.push({method:request.method(),url:request.url()})});
const check = async (name, fn) => { await fn(); checks.push(name); console.log(name); };
const screenshot = name => page.screenshot({path:path.join(values.out,name+'.png'),fullPage:true});
try {
  const response = await page.goto(values.url);
  assert.equal(response.headers()['cross-origin-embedder-policy'],'require-corp');
  await page.locator('#choose-disc:not([disabled])').waitFor({timeout:60000});
  await check('runtime startup and no page decoration', async()=>{
    assert(await page.locator('#start-game').isDisabled());
    assert(await page.locator('#development-link').isHidden());
    assert.equal(await page.locator('h1, header, footer, article').count(),0);
    assert.equal(new URL(page.frames()[1].url()).search,'');
    assert(await page.locator('#end-session').isDisabled());
    const content = await (await page.request.get(new URL('prototype-content.json', values.url).href)).json();
    assert.equal(content.fighters.length,4);
    assert.equal(content.stages.length,4);
  });
  await screenshot('desktop');
  await check('controls dialog keyboard toggles and game focus',async()=>{
    await page.locator('#controls-open').click();
    await page.locator('#keyboard-one').uncheck();
    assert.equal(await page.frameLocator('iframe').locator('#keyboard').isChecked(),false);
    await page.locator('#keyboard-one').check();
    await screenshot('controls');
    await page.keyboard.press('Escape');
    assert(await page.locator('#controls-dialog').isHidden());
    await page.frames()[1].waitForFunction(()=>document.activeElement.id==='canvas');
  });
  await check('B0XX layout, modifiers, key release, persistence and split-layout restore', async()=>{
    const input = () => page.frames()[1];
    const waitPad = async expected => input().waitForFunction(expected => {
      const state = JSON.parse(Module.UTF8ToString(Module._melee_web_input_message()));
      return Object.entries(expected).every(([key,value]) => JSON.stringify(state.pads[0][key]) === JSON.stringify(value));
    }, expected);
    const chooseLayout = async layout => {
      await page.locator('#controls-open').click();
      await page.locator('#keyboard-layout').selectOption(layout);
      assert.equal(await page.locator('#keyboard-two-option').isHidden(), layout === 'boxx');
      await page.locator('#controls-close').click();
      await input().waitForFunction(()=>document.activeElement.id==='canvas');
    };
    await chooseLayout('boxx');
    await input().waitForFunction(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).keyboard_active_mask===1);
    const cases = [
      ['m',{buttons:0x100}], ['o',{buttons:0x200}], ['p',{buttons:0x400}], ['0',{buttons:0x800}],
      ['[',{buttons:0x10}], ['7',{buttons:0x1000}], ['q',{buttons:0x40}], ['9',{buttons:0x20}],
      ['2',{stick:[-80,0]}], ['3',{stick:[0,-80]}], ['4',{stick:[80,0]}], [']',{stick:[0,80]}],
      ['n',{cstick:[-80,0]}], [' ',{cstick:[0,-80]}], [',',{cstick:[80,0]}], ['k',{cstick:[0,80]}],
      ['-',{triggers:[0,49]}], ['=',{triggers:[0,94]}],
      ['ArrowLeft',{buttons:1}], ['ArrowRight',{buttons:2}], ['ArrowDown',{buttons:4}], ['ArrowUp',{buttons:8}],
    ];
    for (const [key,expected] of cases) {
      await page.keyboard.down(key); await waitPad(expected); await page.keyboard.up(key);
      await waitPad({buttons:0,stick:[0,0],cstick:[0,0],triggers:[0,0]});
    }
    await page.keyboard.down('4'); await page.keyboard.down('v'); await waitPad({stick:[53,0]});
    await page.keyboard.up('v'); await page.keyboard.down('b'); await waitPad({stick:[27,0]});
    await page.keyboard.up('b'); await page.keyboard.down('3'); await waitPad({stick:[56,-56]});
    await page.keyboard.down('v'); await waitPad({stick:[59,-25]});
    await page.keyboard.down('q'); await waitPad({stick:[51,-30]});
    for (const key of ['q','v','3','4']) await page.keyboard.up(key);
    await waitPad({buttons:0,stick:[0,0]});
    await page.keyboard.down('2'); await page.keyboard.down('4'); await waitPad({stick:[80,0]});
    await page.keyboard.up('4'); await waitPad({stick:[0,0]}); await page.keyboard.up('2');
    await page.keyboard.down('v'); await page.keyboard.down('b'); await page.keyboard.down('k');
    await waitPad({buttons:8,cstick:[0,0]});
    for (const key of ['k','b','v']) await page.keyboard.up(key);
    await waitPad({buttons:0,stick:[0,0]});
    // Modal focus loss clears a held action; release/repeat cannot leak it back.
    await page.keyboard.down('m'); await waitPad({buttons:0x100});
    await page.locator('#controls-open').click(); await waitPad({buttons:0});
    await screenshot('boxx-controls');
    await page.keyboard.up('m'); await page.locator('#controls-close').click();
    await input().waitForFunction(()=>document.activeElement.id==='canvas'); await waitPad({buttons:0});
    await page.reload(); await page.locator('#choose-disc:not([disabled])').waitFor({timeout:60000});
    assert.equal(await page.locator('#keyboard-layout').inputValue(),'boxx');
    await input().locator('#canvas').click();
    await input().waitForFunction(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).keyboard_active_mask===1);
    await page.keyboard.down('m'); await waitPad({buttons:0x100}); await page.keyboard.up('m');
    await chooseLayout('two');
    await input().waitForFunction(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).keyboard_active_mask===3);
    await page.keyboard.down('m'); await waitPad({buttons:0}); await page.keyboard.up('m');
    await page.keyboard.down('j'); await waitPad({buttons:0x100}); await page.keyboard.up('j');
    await page.keyboard.down('ShiftRight');
    await input().waitForFunction(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).pads[1].buttons===0x100);
    await page.keyboard.up('ShiftRight');
  });
  await check('unsupported disc rejection and selectable retry',async()=>{
    await page.locator('#disc-file').setInputFiles({name:'unsupported.rvz',mimeType:'application/octet-stream',buffer:Buffer.from('invalid')});
    await page.locator('#error-dialog[open]').waitFor({timeout:60000});
    assert.match(await page.locator('#error').innerText(),/RVZ is not supported/);
    assert(await page.locator('#start-game').isDisabled());
    assert(await page.locator('#choose-disc').isEnabled());
    await page.locator('#error-close').click();
    await page.locator('#disc-file').setInputFiles({name:'invalid.iso',mimeType:'application/octet-stream',buffer:Buffer.alloc(2048)});
    await page.waitForFunction(()=>document.querySelector('#error-dialog').open && /expected Super Smash Bros/.test(document.querySelector('#error').textContent));
    assert(await page.locator('#start-game').isDisabled());
    await screenshot('invalid-disc');
    await page.locator('#error-close').click();
  });
  await check('responsive 390px and 320px layouts',async()=>{
    await page.setViewportSize({width:390,height:844});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await screenshot('mobile');
    await page.setViewportSize({width:320,height:740});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth));
    await page.setViewportSize({width:1440,height:1050});
  });
  await check('fullscreen enter/exit or feature-detected fallback',async()=>{
    if (await page.locator('#fullscreen').isEnabled()) {
      await page.locator('#fullscreen').click();
      await page.waitForFunction(()=>document.fullscreenElement || document.querySelector('#error-dialog').open);
      if (await page.evaluate(()=>!!document.fullscreenElement)) {
        await page.locator('#fullscreen').click();
        await page.waitForFunction(()=>!document.fullscreenElement);
      }
    } else assert.match(await page.locator('#fullscreen').getAttribute('title'),/unavailable/);
  });
  if(values.disc) {
    await check('owned-disc validation through existing import and preparation',async()=>{
      await page.locator('#disc-file').setInputFiles(values.disc);
      await page.locator('#start-game:not([disabled])').waitFor({timeout:60000});
      assert(await page.locator('#error-dialog').isHidden());
      await screenshot('prepared');
    });
    await check('caught child launch failure surfaces in the shell',async()=>{
      const launch = page.frameLocator('iframe').locator('#launch');
      // Simulate only a swallowed UI-handler failure; no native state injection.
      await launch.evaluate(button => {
        button.savedHandler = button.onclick;
        button.onclick = async () => { document.querySelector('#status').textContent = 'Launch test error'; };
      });
      await page.locator('#start-game').click();
      await page.locator('#error-dialog[open]').waitFor();
      assert.equal(await page.locator('#error').innerText(),'Launch test error');
      await launch.evaluate(button => { button.onclick = button.savedHandler; delete button.savedHandler; });
      await page.locator('#error-close').click();
    });
    await check('original CSS launch and actual canvas focus',async()=>{
      await page.locator('#start-game').click();
      await page.frameLocator('iframe').locator('#status[data-phase=\"1\"]').waitFor({state:'attached',timeout:60000});
      await page.locator('#pause-game:not([disabled])').waitFor();
      await page.frames()[1].waitForFunction(()=>document.activeElement.id==='canvas');
      await screenshot('original-css');
    });
    await check('pause acknowledgement and rapid-click suppression',async()=>{
      await page.locator('#pause-game').evaluate(button => { button.click(); button.click(); });
      await page.frames()[1].waitForFunction(()=>document.querySelector('#status').textContent==='Paused.');
      await page.locator('#pause-game:not([disabled])').waitFor();
      assert.equal(await page.locator('#pause-game').innerText(),'Resume');
      await page.locator('#pause-game').click();
      await page.frames()[1].waitForFunction(()=>document.querySelector('#status').textContent==='Original character select');
      await page.locator('#pause-game:not([disabled])').waitFor();
      assert.equal(await page.locator('#pause-game').innerText(),'Pause');
    });
    await check('ordinary keyboard CSS to SSS navigation',async()=>{
      // Two logical keyboard layouts through the normal browser input path.
      for (const key of ['j','ShiftRight','Enter']) {
        await page.keyboard.down(key); await page.waitForTimeout(120); await page.keyboard.up(key); await page.waitForTimeout(150);
      }
      await page.frameLocator('iframe').locator('#status[data-phase=\"3\"]').waitFor({state:'attached',timeout:15000});
      await screenshot('original-sss');
    });
    await check('B0XX Start enters SSS with a CPU opponent',async()=>{
      await page.locator('#controls-open').click();
      await page.locator('#keyboard-layout').selectOption('boxx');
      await page.locator('#controls-close').click();
      await page.frames()[1].waitForFunction(()=>document.activeElement.id==='canvas');
      await page.keyboard.down('o'); await page.waitForTimeout(120); await page.keyboard.up('o');
      await page.frameLocator('iframe').locator('#status[data-phase="1"]').waitFor({state:'attached',timeout:15000});
      // Original CSS initializes a 30-source-tick Start lockout on entry.
      // Read existing diagnostics; do not modify source state or inject PAD.
      const entrySteps = await page.frames()[1].evaluate(()=>nativeSourceSteps);
      await page.frames()[1].waitForFunction(steps=>nativeSourceSteps>=steps+31,entrySteps);
      await page.keyboard.down('7');
      await page.frames()[1].waitForFunction(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).pads[0].buttons===0x1000);
      await page.waitForTimeout(120); await page.keyboard.up('7');
      await page.frameLocator('iframe').locator('#status[data-phase="3"]').waitFor({state:'attached',timeout:15000});
    });
    await check('ordinary keyboard stage selection and autonomous CPU movement',async()=>{
      const frame = page.frames()[1];
      await frame.waitForFunction(()=>!!window.menuObserveStage(32));
      // Read the source cursor, then steer with ordinary keyboard events.
      // No native selection writes or diagnostic PAD injection.
      for (let attempt=0; attempt<240; attempt++) {
        const {ids,geometry:[x,y,tx,ty]} = await frame.evaluate(()=>window.menuObserveStage(32));
        if (ids[1]===32) break;
        const keys=[];
        if (Math.abs(tx-x)>1.2) keys.push(tx>x?'4':'2');
        if (Math.abs(ty-y)>1.2) keys.push(ty>y?']':'3');
        if (!keys.length) keys.push(tx>=x?'4':'2');
        for (const key of keys) await page.keyboard.down(key);
        await page.waitForTimeout(20);
        for (const key of keys) await page.keyboard.up(key);
        await page.waitForTimeout(20);
      }
      assert.equal((await frame.evaluate(()=>window.menuObserveStage(32))).ids[1],32);
      await page.keyboard.down('m'); await page.waitForTimeout(120); await page.keyboard.up('m');
      await frame.locator('#status[data-phase="7"]').waitFor({state:'attached',timeout:120000});
      await frame.waitForFunction(()=>window.menuObservePlayer(1)?.frame>=180,null,{timeout:60000});
      const cpuStart=await frame.evaluate(()=>window.menuObservePlayer(1));
      await frame.waitForFunction(start=>{
        const cpu=window.menuObservePlayer(1);
        return cpu && cpu.frame>start.frame+60 && cpu.motion!==14 &&
          (Math.abs(cpu.x-start.x)>2 || cpu.motion!==start.motion);
      },cpuStart,{timeout:60000});
      assert.equal(await frame.evaluate(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).pads[1].err),-1);
      await screenshot('cpu-match');
    });
    await check('CPU match No Contest returns to CSS through ordinary keyboard input',async()=>{
      const frame = page.frames()[1];
      // Screenshot/automation stalls can trigger the host's timing guard.
      // Resume through the visible utility and report it; this is UI evidence,
      // never an uninterrupted gameplay or performance pass.
      await page.waitForTimeout(200);
      if ((await frame.locator('#status').textContent()).includes('timing disruption')) {
        await page.locator('#pause-game').click();
        timingResumes++;
        await frame.waitForFunction(()=>Module._melee_web_native_menu_running());
      }
      await page.keyboard.down('7'); await page.waitForTimeout(100); await page.keyboard.up('7');
      await frame.waitForFunction(()=>Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()).includes('source pause: 1'));
      await page.waitForTimeout(700);
      for (const key of ['q','9','m','7']) await page.keyboard.down(key);
      await page.waitForTimeout(120);
      for (const key of ['7','m','9','q']) await page.keyboard.up(key);
      await page.frames()[1].locator('#status[data-phase="1"]').waitFor({state:'attached',timeout:30000});
      await screenshot('cpu-return-css');
    });
    await check('session teardown releases frame and imported data',async()=>{
      const old = page.frames()[1];
      await page.locator('#end-session').click();
      await page.locator('#choose-disc:not([disabled])').waitFor({timeout:60000});
      assert(old.isDetached());
      assert(await page.locator('#start-game').isDisabled());
      assert.equal(await page.locator('#keyboard-layout').inputValue(),'boxx');
      await page.frames()[1].waitForFunction(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).keyboard_layout===1);
    });
    await check('development runtime launches the same CPU match with one keyboard',async()=>{
      const runtime = await browser.newPage({viewport:{width:1280,height:960}});
      runtime.on('pageerror',error=>errors.push(error.message));
      runtime.on('console',message=>{if(message.type()==='error')errors.push(message.text())});
      try {
        await runtime.goto(new URL('runtime.html',values.url).href);
        await runtime.locator('#disc:not([disabled])').waitFor({timeout:60000});
        await runtime.locator('#controls-open').click();
        await runtime.locator('#keyboard-layout').selectOption('two');
        await runtime.getByLabel('Player 1 input source', {exact:true}).selectOption('keyboard');
        await runtime.getByLabel('Player 2 input source', {exact:true}).selectOption('off');
        await runtime.locator('#controls-close').click();
        await runtime.locator('#disc').setInputFiles(values.disc);
        await runtime.locator('#launch:not([disabled])').waitFor({timeout:60000});
        await runtime.locator('#launch').click();
        await runtime.locator('#status[data-phase="1"]').waitFor({state:'attached',timeout:60000});
        await runtime.locator('#canvas').click();
        const entered=await runtime.evaluate(()=>nativeSourceSteps);
        await runtime.waitForFunction(steps=>nativeSourceSteps>=steps+31,entered);
        for (const key of ['j','Enter']) {
          await runtime.keyboard.down(key); await runtime.waitForTimeout(120);
          await runtime.keyboard.up(key); await runtime.waitForTimeout(150);
        }
        await runtime.locator('#status[data-phase="3"]').waitFor({state:'attached',timeout:15000});
        await runtime.waitForFunction(()=>!!window.menuObserveStage(32));
        for (let attempt=0;attempt<240;attempt++) {
          const {ids,geometry:[x,y,tx,ty]}=await runtime.evaluate(()=>window.menuObserveStage(32));
          if (ids[1]===32) break;
          const keys=[];
          if (Math.abs(tx-x)>1.2) keys.push(tx>x?'d':'a');
          if (Math.abs(ty-y)>1.2) keys.push(ty>y?'w':'s');
          if (!keys.length) keys.push(tx>=x?'d':'a');
          for (const key of keys) await runtime.keyboard.down(key);
          await runtime.waitForTimeout(20);
          for (const key of keys) await runtime.keyboard.up(key);
          await runtime.waitForTimeout(20);
        }
        assert.equal((await runtime.evaluate(()=>window.menuObserveStage(32))).ids[1],32);
        await runtime.keyboard.down('j'); await runtime.waitForTimeout(120); await runtime.keyboard.up('j');
        await runtime.locator('#status[data-phase="7"]').waitFor({state:'attached',timeout:120000});
        await runtime.waitForFunction(()=>window.menuObservePlayer(1)?.frame>=180,null,{timeout:60000});
        const initial=await runtime.evaluate(()=>window.menuObservePlayer(1));
        await runtime.waitForFunction(start=>{
          const cpu=window.menuObservePlayer(1);
          return cpu && cpu.frame>start.frame+60 && cpu.motion!==14 &&
            (Math.abs(cpu.x-start.x)>2 || cpu.motion!==start.motion);
        },initial,{timeout:60000});
        assert.equal(await runtime.evaluate(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())).pads[1].err),-1);
        await runtime.screenshot({path:path.join(values.out,'development-cpu-match.png'),fullPage:true});
        await runtime.locator('#unload').click();
        await runtime.waitForFunction(()=>document.querySelector('#status').textContent==='Native menus unloaded.');
      } catch(error) {
        await runtime.screenshot({path:path.join(values.out,'development-failure.png'),fullPage:true});
        error.message += `\nDevelopment status: ${await runtime.locator('#status').textContent()}`;
        throw error;
      } finally { await runtime.close(); }
    });
  }
  assert.deepEqual(errors,[]); assert.deepEqual(posts,[]);
  checks.push('no page/HTTP errors and no data uploads');
} catch(error) {
  failures.push(error.stack || String(error));
  failureState = await Promise.all(page.frames().map(frame => frame.evaluate(()=>{
    const m=globalThis.Module;
    return {
      status:document.querySelector('#status')?.textContent,
      phase:document.querySelector('#status')?.dataset.phase,
      native:m?._melee_web_native_menu_diagnostics ? m.UTF8ToString(m._melee_web_native_menu_diagnostics()) : null,
      input:m?._melee_web_input_message ? m.UTF8ToString(m._melee_web_input_message()) : null,
    };
  }).catch(()=>null)));
  await screenshot('failure');
} finally {
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify({schema:'melee-web-prototype-ui-check-v1',browser:browser.version(),checks,failures,errors,posts,failureState,timing_resumes:timingResumes,owned_disc_used:!!values.disc,gameplay_admission:false,performance_admission:false},null,2)+'\n');
  await browser.close();
}
if (failures.length) { console.error(failures.join('\n')); process.exitCode=1; }
