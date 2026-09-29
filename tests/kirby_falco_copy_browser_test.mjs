#!/usr/bin/env node
/**
 * Rendered, source-input Kirby/Falco copy prefix.
 *
 * Uses the original CSS and SSS, two human slots, and ordinary P1 source PAD
 * samples to approach Falco, inhale, acquire the copy, and use neutral special.
 * This is a visual/action prefix, not a full natural Results loop, retail
 * comparison, pixel-equivalence, foreground-timing, or performance claim.
 */
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, loadBrowserTools} from '../scripts/browser_tools.mjs';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';

const {values}=parseArgs({options:Object.fromEntries(
  ['url','disc','out','playwright','build-dir'].map(name=>[name,{type:'string'}]))});
if(!values.url||!values.disc||!values.out)
  throw Error('Use --url DEVELOPMENT_RUNTIME_URL --disc OWNED_DISC --out NEW_DIRECTORY [--playwright PACKAGE_DIR] [--build-dir BUILT_RUNTIME_DIR]');
const output=path.resolve(values.out);
await fs.mkdir(output,{recursive:false});
const repository=path.resolve(import.meta.dirname,'..');
const digest=async filename=>{
  const hash=createHash('sha256');
  for await(const chunk of (await import('node:fs')).createReadStream(filename))hash.update(chunk);
  return hash.digest('hex');
};
const git=(...args)=>execFileSync('git',args,{cwd:repository,encoding:'utf8'}).trim();
const report={schema:'melee-web-kirby-falco-copy-rendered-prefix-v1',
  scope:'Headless rendered original CSS→SSS→Final Destination→two-human match; P1 source PAD approaches and copies P2 Falco. Source trace separately covers copy loss/reacquisition/teardown. No retail comparison, full Results loop, pixel equivalence, foreground timing, or performance claim.',
  result:'fail',url:values.url,browser:null,gpu:null,players:[],css_setup:null,
  input_profile:null,source_inputs:[],source_progress:[],screenshots:[],
  source_timing_disruptions:[],artifact_read_failures:[],page_errors:[],native_command_errors:[],
  target_crashes:[],served_artifacts:[],phases:[]};
const buildDir=values['build-dir']?path.resolve(values['build-dir']):null;
if(buildDir){
  const wasm=path.join(buildDir,'gameplay_menu_browser.wasm');
  report.local_wasm={path:path.relative(repository,wasm),bytes:(await fs.stat(wasm)).size,sha256:await digest(wasm)};
}
report.harness_sha256=await digest(new URL(import.meta.url));
report.provenance_start={commit:git('rev-parse','HEAD'),tree:git('rev-parse','HEAD^{tree}'),
  status:git('status','--porcelain=v1'),tracked_diff_sha256:createHash('sha256')
    .update(execFileSync('git',['diff','--binary','HEAD'],{cwd:repository})).digest('hex')};
const artifactReads=[];
let browser,page,driver;
const buttonA=0x0100,buttonB=0x0200,buttonStart=0x1000;

async function state(){
  return page.evaluate(()=>{
    const text=id=>document.querySelector(id)?.textContent||'';
    const phase=Module._melee_web_native_menu_phase();
    const match=JSON.parse(Module.UTF8ToString(Module._melee_web_native_menu_match_observe()));
    return {phase,running:Module._melee_web_native_menu_running(),
      status:text('#status'),error:document.querySelector('#status')?.dataset.runtimeError||null,
      diagnostics:Module.UTF8ToString(Module._melee_web_native_menu_diagnostics()),
      runtimeLog:text('#log'),nativeCommandError:window.__meleeWebUnsupportedCommand||null,
      assetFatal:window.__meleeWebFighterAssetFatal||null,
      input:JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())),match,
      p0:phase===7?window.menuObservePlayer(0):null,
      p1:phase===7?window.menuObservePlayer(1):null,
      css:phase===1?window.menuObserveCssSetup():null};
  });
}
async function resumeTimingPause(snapshot){
  if(!snapshot.status.startsWith('Paused after a timing disruption'))return snapshot;
  report.source_timing_disruptions.push({phase:snapshot.phase,
    source_frame:snapshot.p0?.frame??null,action:'resumed-at-same-source-frame'});
  await page.evaluate(()=>Module._melee_web_native_menu_pause(0));
  await page.waitForTimeout(80);
  const resumed=await state();
  if(resumed.error)throw Error(`Timing-resume boundary: ${resumed.error}`);
  return resumed;
}
async function waitFor(label,predicate,timeoutMs=30000){
  const deadline=Date.now()+timeoutMs;
  while(Date.now()<deadline){
    let current=await state();
    current=await resumeTimingPause(current);
    if(current.error)throw Error(`${label}: ${current.error}`);
    if(current.nativeCommandError)throw Error(`${label}: ${current.nativeCommandError}`);
    if(current.assetFatal)throw Error(`${label}: ${JSON.stringify(current.assetFatal)}`);
    if(predicate(current))return current;
    await page.waitForTimeout(12);
  }
  throw Error(`${label} timed out: ${JSON.stringify(await state())}`);
}
async function waitForNoQueuedPad(label,timeoutMs=3000,record=true){
  const deadline=Date.now()+timeoutMs;
  while(Date.now()<deadline){
    const current=await resumeTimingPause(await state());
    if(current.error)throw Error(`${label}: ${current.error}`);
    if(current.diagnostics.includes('raw PAD: none')){
      if(record)report.phases.push({label,diagnostics:current.diagnostics});
      return current;
    }
    await page.waitForTimeout(20);
  }
  throw Error(`${label}: raw PAD did not drain: ${(await state()).diagnostics}`);
}
async function sourceFrame(target,label){
  const deadline=Date.now()+20000;
  while(Date.now()<deadline){
    let current=await state();
    current=await resumeTimingPause(current);
    if(current.error)throw Error(`${label}: ${current.error}`);
    if(current.phase!==7)throw Error(`${label}: match left VS before source frame ${target}`);
    if(current.p0&&current.p0.frame>=target)return current;
    await page.waitForTimeout(8);
  }
  throw Error(`${label}: source frame did not reach ${target}; ${JSON.stringify(await state())}`);
}
async function sample(port,buttons=0,stickX=0,stickY=0,duration=1,label='input'){
  assert(Number.isInteger(duration)&&duration>=1&&duration<=120);
  let before=await state();
  if(!before.diagnostics.includes('raw PAD: none'))
    before=await waitForNoQueuedPad(`${label} prior PAD sample`);
  const sourceFrameBefore=before.p0?.frame??null;
  const queued=await page.evaluate(args=>window.menuDiagnosticPad(...args),
    [port,buttons,stickX,stickY,duration]);
  if(queued===0)throw Error(`${label}: source PAD rejected the sample`);
  if(sourceFrameBefore!==null)
    await sourceFrame(sourceFrameBefore+duration,`${label} source consumption`);
  else{
    await page.waitForTimeout(Math.max(40,duration*18));
    await resumeTimingPause(await state());
  }
  let after=await state();
  const queuedAtSourceBoundary=!after.diagnostics.includes('raw PAD: none');
  if(queuedAtSourceBoundary)
    after=await waitForNoQueuedPad(`${label} post-boundary PAD drain`,3000,false);
  report.source_inputs.push({port,buttons,stickX,stickY,duration,label,
    source_frame_before:sourceFrameBefore,source_frame_after:after.p0?.frame??null,
    queued_at_source_boundary:queuedAtSourceBoundary,
    queued_after_sample:!after.diagnostics.includes('raw PAD: none')});
  return after;
}
async function tap(port,button,label,duration=1){
  await sample(port,button,0,0,duration,label);
  await sample(port,0,0,0,2,`${label}:release`);
}
async function capture(name){
  const full=path.join(output,`${name}.png`),canvas=path.join(output,`${name}-canvas.png`);
  await page.screenshot({path:full,fullPage:false});
  await page.locator('#canvas').screenshot({path:canvas});
  report.screenshots.push({name,full,canvas});
}
function row(values,index,width){return values.slice(index*width,(index+1)*width);}
async function css(){
  const current=await state();
  assert.equal(current.phase,1,`Original CSS required, phase=${current.phase}`);
  assert(current.css?.cursors?.length===16&&current.css?.doors?.length===40&&
    current.css?.geometry?.length===48,'Read-only source CSS observer unavailable');
  return current.css;
}
async function driveFighter(port,door,fighter){
  for(let step=0;step<480;step++){
    const observation=await page.evaluate(({port,kind})=>
      window.menuObserveFighterPort(port,kind),{port,kind:fighter.kind});
    assert(observation,`Source CSS geometry unavailable for ${fighter.name} on P${port+1}`);
    const [cursorPort,heldDoor]=observation.ids;
    const [cursorX,cursorY,observedModelX,observedModelY,left,right,top,bottom]=observation.geometry;
    const doorGeometry=row((await css()).geometry,door,12);
    const modelX=door===port?observedModelX:doorGeometry[2];
    const modelY=door===port?observedModelY:doorGeometry[3];
    assert.equal(cursorPort,port,`${fighter.name}: wrong CSS cursor port`);
    const inside=modelX>left&&modelX<right&&modelY<top&&modelY>bottom;
    const setup=await css(),doorState=row(setup.doors,door,10);
    if(doorState[3]===fighter.kind&&heldDoor<0&&inside)return;
    let targetX,targetY,ready=false;
    if(heldDoor<0){targetX=modelX-3.8;targetY=modelY+2.6;
      ready=(targetX-cursorX)**2+(targetY-cursorY)**2<9;}
    else{assert.equal(heldDoor,door,`${fighter.name}: wrong held CSS door`);
      targetX=(left+right)/2-2.7;targetY=(top+bottom)/2+2.0;ready=inside;}
    if(ready){await tap(port,buttonA,`source-place-${fighter.name}-door-${door}`);continue;}
    const axis=(current,target)=>current<target-0.62?80:current>target+0.62?-80:0;
    const stickX=axis(cursorX,targetX),stickY=axis(cursorY,targetY);
    await sample(port,0,stickX,stickY,1,`source-drive-${fighter.name}-door-${door}`);
  }
  throw Error(`Source CSS did not select ${fighter.name} on door ${door}`);
}
async function chooseHuman(port,fighter){
  await driveFighter(port,port,fighter);
  await waitFor(`${fighter.name} CSS selection`,current=>
    row(current.css?.doors||[],port,10)[3]===fighter.kind);
  report.phases.push(`source CSS confirmed ${fighter.name} on P${port+1}`);
}
async function chooseFD(){
  await tap(0,buttonStart,'CSS-to-SSS Start',8);
  let entered=await waitFor('original SSS entry',current=>current.phase===3,2500).catch(()=>null);
  if(!entered){await driver.pressChord(['Enter'],{holdMs:120,releaseMs:100});
    entered=await waitFor('original SSS entry after trusted Enter',current=>current.phase===3,30000);}
  report.phases.push('original SSS entered through controller input');
  await capture('original-sss');
  await waitForNoQueuedPad('SSS transition input drained before stage driver');
  let reached=false;
  for(let attempt=0;attempt<360;attempt++){
    const result=await page.evaluate(()=>Module._melee_web_native_menu_drive_stage(32));
    if(result===0)throw Error('Source SSS stage driver rejected Final Destination');
    if(result===2){reached=true;break;}
    await waitForNoQueuedPad('source SSS direction sample consumed',3000,false);
  }
  assert(reached,'Original SSS did not highlight Final Destination');
  const stage=await page.evaluate(()=>window.menuObserveStage(32));
  report.stage=stage;
  assert.equal(stage.ids[1],32,'Final Destination source selection mismatch');
  await capture('final-destination-highlighted');
  await tap(0,buttonA,'confirm-Final-Destination');
  await waitFor('original match entry',current=>current.phase===7,60000);
}

try{
  const {chromium,browser:launchOptions,browserPath,playwrightPath}=
    await loadBrowserTools(values.playwright);
  browser=await chromium.launch({...browserLaunchOptions(launchOptions,{timeout:60000}),headless:true});
  report.browser={name:'headless Chrome',executable:path.basename(browserPath),
    version:browser.version(),playwright:playwrightPath};
  page=await browser.newPage({viewport:{width:1280,height:960},deviceScaleFactor:1});
  page.setDefaultTimeout(60000);page.setDefaultNavigationTimeout(60000);
  page.on('pageerror',error=>report.page_errors.push(error.stack||error.message));
  page.on('crash',()=>report.target_crashes.push('page crash'));
  page.on('console',message=>{if(message.type()==='error')report.page_errors.push(message.text());});
  page.on('response',response=>{
    if(response.status()>=400)report.native_command_errors.push(`${response.status()} ${response.url()}`);
    if(/\/gameplay_menu_browser\.(js|wasm)$/.test(new URL(response.url()).pathname)){
      const metadata={url:response.url(),status:response.status(),
        content_length:response.headers()['content-length']||null};
      report.served_artifacts.push(metadata);
      artifactReads.push(response.body().then(bytes=>report.served_artifacts.push({
        ...metadata,bytes:bytes.length,
        sha256:createHash('sha256').update(bytes).digest('hex')}))
        .catch(error=>report.artifact_read_failures.push({url:response.url(),message:error.message})));
    }
  });
  const response=await page.goto(values.url,{waitUntil:'domcontentloaded'});
  assert.equal(response.status(),200,'Development runtime must load over HTTP');
  assert.equal(response.headers()['cross-origin-opener-policy'],'same-origin');
  assert.equal(response.headers()['cross-origin-embedder-policy'],'require-corp');
  report.gpu=await page.evaluate(async()=>{
    const adapter=await navigator.gpu?.requestAdapter();
    return {cross_origin_isolated:crossOriginIsolated,adapter_available:!!adapter,
      info:adapter?{vendor:adapter.info.vendor,architecture:adapter.info.architecture}:null};
  });
  assert(report.gpu.cross_origin_isolated&&report.gpu.adapter_available,
    'Rendered check requires isolated headless Chrome with WebGPU');
  driver=createBrowserDriver(page,{timeoutMs:60000,deadline:Date.now()+12*60*1000});
  await driver.waitForImport();await driver.selectDisc(values.disc);await driver.waitForStart();
  await driver.launch(1);
  await waitFor('original CSS',current=>current.phase===1&&current.css,60000);
  const input=await page.evaluate(()=>JSON.parse(Module.UTF8ToString(Module._melee_web_input_message())));
  report.input_profile={keyboard_requested_mask:input.keyboard_requested_mask,
    keyboard_active_mask:input.keyboard_active_mask,physical_mask:input.physical_mask,
    port_errors:input.pads.map(pad=>pad.err)};
  await capture('original-css-before-copy');
  await chooseHuman(0,{name:'Kirby',kind:4});
  await chooseHuman(1,{name:'Falco',kind:20});
  const setup=await css();
  report.css_setup=Array.from({length:4},(_,door)=>{
    const value=row(setup.doors,door,10);
    return {door,kind:value[0],character:value[3],slot_type:value[4],cpu_level:value[5]};
  });
  assert.deepEqual(report.css_setup.slice(0,2).map(player=>player.character),[4,20]);
  assert(report.css_setup.slice(0,2).every(player=>player.kind===0&&player.slot_type===0),
    'Kirby and Falco must remain source-confirmed human slots');
  assert(report.css_setup.slice(2).every(player=>player.kind===3&&player.slot_type===3),
    'Unused CSS doors must remain inactive');
  report.phases.push({label:'source-confirmed two-human roster',players:report.css_setup});
  await chooseFD();
  await waitFor('ready source match players',current=>current.phase===7&&current.p0&&current.p1&&
    current.p0.frame>=30,60000);
  report.match_start=await state();
  assert.equal(report.match_start.p0.fighterKind,4,'P1 must be native FTKIND_KIRBY');
  assert.equal(report.match_start.p1.fighterKind,22,'P2 must be native FTKIND_FALCO');
  await capture('match-before-inhale');

  let approaching=null;
  for(let attempt=0;attempt<360;attempt++){
    const current=await state();
    if(current.p0?.groundAir===0&&current.p1?.groundAir===0&&
       Math.abs(current.p1.x-current.p0.x)<7&&Math.abs(current.p1.y-current.p0.y)<5){
      approaching=current;break;
    }
    const dx=current.p1.x-current.p0.x,dy=current.p1.y-current.p0.y;
    const stickX=Math.abs(dx)<3?0:dx>0?80:-80;
    const stickY=Math.abs(dy)<4?0:dy>0?60:-60;
    await sample(0,0,stickX,stickY,1,'Kirby approach Falco');
  }
  assert(approaching,'P1 Kirby did not reach grounded Falco within source inhale range');
  report.approach={kirby:approaching.p0,falco:approaching.p1,
    delta_x:approaching.p1.x-approaching.p0.x,delta_y:approaching.p1.y-approaching.p0.y};
  let swallowed=null;
  for(let pulse=0;pulse<30&&!swallowed;pulse++){
    const current=await sample(0,buttonB,0,0,1,`Kirby inhale pulse ${pulse+1}`);
    if(current.p0?.motion===358||current.p0?.motion===359)swallowed=current;
    if(!swallowed)await sample(0,0,0,0,7,`Kirby inhale release ${pulse+1}`);
  }
  assert(swallowed,'Kirby source neutral-B did not capture Falco into ftKb_MS_Eat (358)');
  report.phases.push({label:'Falco captured into source Kirby Eat motion',
    kirby:swallowed.p0,falco:swallowed.p1});
  const eatWait=swallowed.p0.motion===359?swallowed:
    await waitFor('source Kirby Eat motion completed into EatWait',current=>current.p0?.motion===359,10000);
  report.phases.push({label:'Kirby swallowed Falco; source EatWait observed',
    kirby:eatWait.p0,falco:eatWait.p1});
  await sample(0,0,0,0,5,'settle Kirby EatWait');
  await capture('kirby-eatwait-falco-captured');
  await sample(0,buttonB,0,0,1,'copy Falco ability from EatWait');
  let copied=null;
  for(let tick=0;tick<240;tick++){
    const current=await state();
    if(current.p0?.motion!==359&&current.p0?.motion===14&&current.p0?.groundAir===0){copied=current;break;}
    await sample(0,0,0,0,1,'wait for source copy completion');
  }
  assert(copied,'Kirby did not finish the source EatWait copy transition to grounded Wait');
  report.phases.push({label:'Falco copy completion observed at grounded Kirby Wait',
    kirby:copied.p0,falco:copied.p1});
  let separated=await sample(1,0,-80,0,32,'move released Falco away for copy-visibility view');
  for(let attempt=0;Math.abs(separated.p1.x-separated.p0.x)<18&&attempt<2;attempt++)
    separated=await sample(1,0,-80,0,16,'continue separating Falco from Kirby');
  assert(Math.abs(separated.p1.x-separated.p0.x)>=18,
    'Falco must be separated from Kirby before inspecting copied-part visibility');
  report.phases.push({label:'released Falco separated from copied Kirby for visual inspection',
    kirby:separated.p0,falco:separated.p1,
    delta_x:separated.p1.x-separated.p0.x});
  await sample(0,0,0,0,30,'render copied Falco parts');
  await capture('kirby-falco-copy-visible');
  const specialStart=await sample(0,buttonB,0,0,1,'start copied Falco neutral special');
  assert.equal(specialStart.p0?.motion,520,
    'Copied Falco neutral-B must enter ftKb_MS_FcSpecialNStart (520)');
  report.phases.push({label:'copied Falco neutral special start observed',
    kirby:specialStart.p0,falco:specialStart.p1});
  await capture('kirby-falco-copy-special-active');
  await sample(0,0,0,0,30,'observe copied Falco special');
  const used=await state();
  assert.equal(used.p0?.motion,522,
    'Copied Falco neutral special must complete to ftKb_MS_FcSpecialNEnd (522)');
  report.phases.push({label:'copied Falco neutral special completed',kirby:used.p0,falco:used.p1});
  await capture('kirby-falco-copy-special');
  report.final_source_state=used;
  report.result='rendered-copy-prefix-pass';
}catch(error){
  report.failure={message:error.message,stack:error.stack,diagnostics:page?await state().catch(e=>({error:e.message})):null};
  process.exitCode=1;
  if(page)await capture('failure').catch(()=>{});
}finally{
  await Promise.allSettled(artifactReads);
  if(page)report.final_diagnostics=await state().catch(error=>({error:error.message}));
  report.page_error_count=report.page_errors.length;
  report.native_command_error_count=report.native_command_errors.length;
  report.target_crash_count=report.target_crashes.length;
  report.provenance_end={commit:git('rev-parse','HEAD'),tree:git('rev-parse','HEAD^{tree}'),
    status:git('status','--porcelain=v1'),tracked_diff_sha256:createHash('sha256')
      .update(execFileSync('git',['diff','--binary','HEAD'],{cwd:repository})).digest('hex')};
  if(driver)driver.dispose();
  if(browser)await browser.close();
  await fs.writeFile(path.join(output,'report.json'),JSON.stringify(report,null,2)+'\n');
}
if(report.result!=='rendered-copy-prefix-pass')throw Error(report.failure?.message||'Kirby/Falco rendered copy prefix failed');
console.log(JSON.stringify({result:report.result,browser:report.browser,match_frame:report.final_source_state?.match?.frame,output}));
