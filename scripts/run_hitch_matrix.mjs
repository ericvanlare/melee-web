#!/usr/bin/env node
/** Headed, sequential replay driver. Only public page controls supply game input.
 * A frozen Python ledger owns the repetition bound; this driver cannot retry slots.
 * Usage and evidence limitations: docs/HITCH_CAPTURE.md.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {parseArgs} from 'node:util';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = async p => JSON.parse(await fs.readFile(p, 'utf8'));
const save = async (p, value) => fs.writeFile(p, JSON.stringify(value, null, 2)+'\n', {flag:'wx'});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const cmd = (exe, args) => execFileSync(exe, args, {cwd:ROOT, encoding:'utf8', maxBuffer:8*1024*1024}).trim();
const MAX_ARTIFACT_BYTES=256*1024*1024;
export function remainingTimeout(deadline, maximum=30000) {
  const remaining=deadline-Date.now();
  if(remaining<=0)throw Error('Frozen slot wall-time bound exhausted');
  return Math.max(1,Math.min(maximum,remaining));
}
export async function verifyServedArtifacts(machine,deadline=Date.now()+300000) {
  const observed={};
  for(const [name,expected] of Object.entries(machine.artifacts)) {
    const url=new URL(name,machine.url);
    const response=await fetch(url,{cache:'no-store',signal:AbortSignal.timeout(remainingTimeout(deadline))});
    if(!response.ok)throw Error(`Served artifact ${name}: HTTP ${response.status}`);
    const hash=crypto.createHash('sha256');let bytes=0;
    for await(const block of response.body) {
      bytes+=block.length;
      if(bytes>MAX_ARTIFACT_BYTES)throw Error('Served artifact exceeds byte bound: '+name);
      hash.update(block);
    }
    const actual=hash.digest('hex');
    if(actual!==expected)throw Error('Served artifact changed: '+name);
    observed[name]={sha256:actual,bytes};
  }
  return observed;
}
export const LAUNCH = {
  channel:'chrome', headless:false, chromiumSandbox:true,
  args:['--enable-automation'],
  viewport:{width:1024,height:1000}, deviceScaleFactor:2,
  // Retain ordinary foreground/background scheduling and audible audio.
  ignoreDefaultArgs:['--mute-audio','--disable-background-timer-throttling',
    '--disable-backgrounding-occluded-windows','--disable-renderer-backgrounding','--enable-unsafe-swiftshader'],
};
export const TRACE = {
  transferMode:'ReturnAsStream', streamFormat:'json', streamCompression:'gzip',
  traceConfig:{recordMode:'recordUntilFull', traceBufferSizeInKb:131072,
    enableSampling:true,
    // The first matrix lost both long-workload traces with broad GPU/compositor
    // categories. Retain CPU stacks, scheduling and explicit boundary marks;
    // completeness still requires the loss/size checks, never this smaller list.
    includedCategories:['toplevel','devtools.timeline','blink.user_timing',
      'v8','disabled-by-default-v8.cpu_profiler','renderer.scheduler']},
};

async function playwright(modulePath) {
  return modulePath ? import(pathToFileURL(path.join(path.resolve(modulePath),'index.mjs')).href) : import('playwright');
}

async function startBrowser(pw, directory) {
  const context = await pw.chromium.launchPersistentContext(directory, LAUNCH);
  try {
  const page = context.pages()[0] || await context.newPage();
  page.setDefaultTimeout(30000);
  const cdp = await context.newCDPSession(page);
  // Playwright normally emulates focus; our visibility failures must be real.
  await cdp.send('Emulation.setFocusEmulationEnabled', {enabled:false});
  await page.bringToFront();
  const version = await cdp.send('Browser.getVersion');
  const commandLine = await cdp.send('Browser.getBrowserCommandLine');
  const browserCdp=await context.browser().newBrowserCDPSession();
  const systemInfo=await browserCdp.send('SystemInfo.getInfo');
  await browserCdp.detach();
  return {context,page,cdp,version,commandLine,systemInfo};
  } catch(error) {await context.close();throw error;}
}

async function profile(options, pw) {
  const browser = await startBrowser(pw, path.resolve(options['browser-profile']));
  try {
    const python = options.python || path.join(ROOT,'.venv/bin/python');
    const files = JSON.parse(cmd(python,['-c',
      "import json,sys;sys.path.insert(0,'tools');from browser_replay_validation import BUILD_ARTIFACTS;print(json.dumps(BUILD_ARTIFACTS))"]));
    const artifacts = {};
    for(const file of files) artifacts[file] = sha(await fs.readFile(path.join(options.build,file)));
    const result = {
      schema:'melee-web-hitch-browser-profile',version:1,build:'Release',
      base_commit:cmd('git',['rev-parse','HEAD']),
      diff_sha256:sha(cmd('git',['diff','--binary'])),
      runner_sha256:sha(await fs.readFile(fileURLToPath(import.meta.url))),
      harness_artifacts:Object.fromEntries(await Promise.all(
        ['scripts/hitch_capture.py','tools/hitch_capture.py','tools/browser_replay_validation.py'].map(async file=>
          [file,sha(await fs.readFile(path.join(ROOT,file)))]))),
      browser:browser.version,command_line:browser.commandLine,launch:LAUNCH,trace:TRACE,node:process.version,
      gpu:browser.systemInfo.gpu,
      focus_emulation:false,artifacts,build_directory:path.resolve(options.build),
      application_reset:'Fresh document and Wasm heap per attempt; one browser context/process and its driver caches retained across the matrix.',
      browser_profile:path.resolve(options['browser-profile']),
      url:options.url||'http://127.0.0.1:8791/runtime.html',
      playwright_module:options.playwright?path.resolve(options.playwright):'playwright',
      playwright_version:options.playwright?(await read(path.join(options.playwright,'package.json'))).version:null,
      resolution:[640,480],device_pixel_ratio:2,
      hardware:cmd('sysctl',['-n','hw.model','hw.memsize','machdep.cpu.brand_string']),
      OS:cmd('sw_vers',[]),power:cmd('pmset',['-g','batt']),
      power_settings:cmd('pmset',['-g','custom']),display:cmd('system_profiler',['SPDisplaysDataType']),
      scope:'Visible headed Chrome development diagnosis. No CPU throttle, state observer, hidden source ticks, synthetic warmup, or driver cache control. Profiler rows are never acceptance evidence.',
    };
    await save(options.out,result);
    console.log(JSON.stringify({profile:options.out,browser:browser.version.product}));
  } finally {await browser.context.close();}
}

export async function stopTrace(cdp, directory, maxBytes=256*1024*1024) {
  const complete = new Promise(resolve => cdp.once('Tracing.tracingComplete',resolve));
  await cdp.send('Tracing.end');
  let timer,info;
  try {
    info=await Promise.race([complete,new Promise((_,reject)=>{
      timer=setTimeout(()=>reject(Error('Trace finalization timeout')),30000);
    })]);
  } finally {clearTimeout(timer);}
  const tracePath = path.join(directory,'chrome-trace.json.gz');
  const output = await fs.open(tracePath,'wx');
  let bytes=0, bytesRead=0, truncated=false,readError=null;
  try {
    if(!info.stream) throw Error('Trace did not return a stream');
    for(;;) {
      const chunk = await cdp.send('IO.read',{handle:info.stream,size:1024*1024});
      const data = Buffer.from(chunk.data,chunk.base64Encoded?'base64':'utf8');
      bytesRead+=data.length;
      if(bytes+data.length>maxBytes){truncated=true;break;}
      await output.write(data);
      bytes+=data.length;
      if(chunk.eof)break;
    }
  } catch(error) {
    readError=String(error);truncated=true;
  } finally {
    await output.close();
    if(info.stream)try{await cdp.send('IO.close',{handle:info.stream});}catch(error){readError=String(error);}
  }
  const metadata = path.join(directory,'trace-metadata.json');
  const valid=!!info.stream&&!truncated&&!readError&&info.dataLossOccurred===false;
  await save(metadata,{...info,bytes,bytes_read:bytesRead,truncated,read_error:readError,configuration:TRACE,
    diagnostic_only:true,complete:valid});
  return {paths:[tracePath,metadata],complete:valid};
}

async function publicReport(page, deadline) {
  while(Date.now()<deadline) {
    const raw = await page.locator('#retail-replay-report').textContent({timeout:remainingTimeout(deadline,5000)});
    if(raw.trim().startsWith('{')) {
      const report = JSON.parse(raw);
      if(report.schema==='melee-web-browser-retail-replay')return report;
    }
    await delay(remainingTimeout(deadline,2000)); // Node-side polling; no per-tick observers.
  }
  throw Error('Frozen slot wall-time bound exhausted');
}

async function run(options,pw) {
  const planPath=path.resolve(options.plan),plan=await read(planPath);
  const machine=await read(plan.identities.profile.path);
  if(options.build&&path.resolve(options.build)!==machine.build_directory)throw Error('--build differs from the frozen profile');
  if(machine.runner_sha256!==sha(await fs.readFile(fileURLToPath(import.meta.url))))throw Error('Runner changed after profile freeze');
  for(const [file,digest] of Object.entries(machine.harness_artifacts||{}))
    if(sha(await fs.readFile(path.join(ROOT,file)))!==digest)throw Error('Harness changed after freeze: '+file);
  if(JSON.stringify(machine.launch)!==JSON.stringify(LAUNCH)||JSON.stringify(machine.trace)!==JSON.stringify(TRACE))throw Error('Browser configuration changed');
  const python=options.python||path.join(ROOT,'.venv/bin/python');
  const ledger=(verb,args=[])=>JSON.parse(cmd(python,[path.join(ROOT,'scripts/hitch_capture.py'),verb,'--plan',planPath,...args]));
  // Validation happens before touching the browser, and again before every slot.
  ledger('status');
  const browser=await startBrowser(pw,machine.browser_profile);
  const {page,cdp}=browser;
  try {
    if(browser.version.product!==machine.browser.product||browser.version.revision!==machine.browser.revision)throw Error('Browser changed after profile freeze');
    if(JSON.stringify(browser.commandLine)!==JSON.stringify(machine.command_line))throw Error('Browser launch arguments changed after profile freeze');
    for(const slot of plan.slots) {
      const state=ledger('status');
      const row=state.attempts.find(r=>r.slot_id===slot.slot_id);
      if(row?.consumed)continue;
      if(row?.state==='open')throw Error('Interrupted slot must be explicitly closed before resuming: '+slot.slot_id);
      const started=ledger('start',['--slot',slot.slot_id]);
      const directory=started.attempt_dir;
      const deadline=Date.now()+plan.timeout_ms;
      const errors=[],attachments=[];let tracing=false,reportPath=null,reason=null,replayStarted=false;
      const pageError=e=>errors.push({type:'pageerror',message:String(e),at:new Date().toISOString()});
      const consoleError=m=>{if(m.type()==='error')errors.push({type:'console',message:m.text(),location:m.location(),at:new Date().toISOString()});};
      page.on('pageerror',pageError);page.on('console',consoleError);
      console.log(JSON.stringify({event:'started',slot:slot.slot_id,target:slot.target_id,mode:slot.mode,cache:slot.cache}));
      try {
        // Fetch outside the source clock, after the durable slot reservation.
        // A server pointing to another build consumes a failed slot too.
        const served=await verifyServedArtifacts(machine,deadline);
        const servedPath=path.join(directory,'served-build.json');await save(servedPath,served);attachments.push(servedPath);
        const url=new URL(machine.url);url.searchParams.set('hitch-capture','1');
        url.searchParams.set('hitch-marks',slot.mode==='profiler'?'1':'0');
        url.searchParams.set('hitch-causal',slot.mode==='profiler'?'1':'0');
        if(slot.cache==='cold')url.searchParams.set('render-cache','clear');
        await page.goto(url.href,{waitUntil:'load',timeout:remainingTimeout(deadline,60000)});
        await page.bringToFront();
        await cdp.send('Emulation.setFocusEmulationEnabled',{enabled:false});
        await page.locator('#disc').setInputFiles(path.resolve(options.disc),{timeout:remainingTimeout(deadline,60000)});
        while(!((await page.locator('#status').textContent({timeout:remainingTimeout(deadline)})).includes('Local menu data loaded.'))) {
          if(Date.now()>deadline)throw Error('Disc preparation exceeded frozen wall-time bound');
          await delay(remainingTimeout(deadline,500));
        }
        await page.getByText('Diagnostics',{exact:true}).click({timeout:remainingTimeout(deadline)});
        const recipe=plan.identities.development_recipes[slot.target_id];
        await page.locator('#retail-replay-file').setInputFiles(recipe.path,{timeout:remainingTimeout(deadline)});
        await page.locator('#retail-replay-mode').selectOption('performance',{timeout:remainingTimeout(deadline)});
        if(slot.mode==='profiler'){await cdp.send('Tracing.start',TRACE);tracing=true;}
        await page.locator('#retail-replay-start').click({timeout:remainingTimeout(deadline)});
        replayStarted=true;
        const report=await publicReport(page,deadline);
        reportPath=path.join(directory,'browser-report.json');await save(reportPath,report);
        if(!report.diagnostic_capture?.enabled)throw Error('Requested hitch capture was not enabled');
        const syncProbe=report.diagnostic_capture.capabilities?.cache_sync;
        if(slot.mode==='profiler'&&!(syncProbe?.requested&&syncProbe.enabled&&syncProbe.installed))
          throw Error('Requested causal cache sync hook was not installed and enabled');
        if(slot.mode==='unprofiled'&&syncProbe?.requested)
          throw Error('Unprofiled slot unexpectedly enabled causal sync diagnostics');
      } catch(error) {
        reason=String(error.stack||error);
        // Stop a timed-out/failed replay through its public control and retain
        // its partial report. This is failure teardown, never a timing resume.
        if(replayStarted&&!reportPath)try {
          await page.locator('#pause').click({timeout:3000});
          const partial=await publicReport(page,Date.now()+5000);
          reportPath=path.join(directory,'browser-report.json');await save(reportPath,partial);
        }catch(teardownError){reason+='\nFailure teardown: '+String(teardownError);}
      }
      finally {
        if(tracing)try{
          const trace=await stopTrace(cdp,directory);attachments.push(...trace.paths);
          if(!trace.complete)reason=(reason||'')+'\nTrace incomplete; retained with loss metadata';
        }catch(error){reason=(reason||'')+'\nTrace capture: '+String(error);}
        // Public DOM only, collected after the source clock stops or an attempt fails.
        const dom={};
        for(const id of ['status','cache-status','render-metrics','perf-metrics','audio-metrics','log','retail-replay-report']) {
          try{dom[id]=await page.locator('#'+id).textContent({timeout:3000});}catch(e){dom[id]=String(e);}
        }
        const domPath=path.join(directory,'terminal-dom.json'),errorPath=path.join(directory,'browser-errors.json');
        await save(domPath,dom);await save(errorPath,errors);attachments.push(domPath,errorPath);
        page.off('pageerror',pageError);page.off('console',consoleError);
        if(errors.length)reason=(reason||'')+'\nBrowser errors retained';
        const failedStatus=Date.now()>=deadline&&!reportPath?'timeout':'aborted';
        const args=['--slot',slot.slot_id,'--status',reason?failedStatus:'completed'];
        if(reportPath)args.push('--report',reportPath);
        if(reason)args.push('--reason',reason);
        args.push('--attachments',...attachments);
        const finished=ledger('finish',args);
        console.log(JSON.stringify({event:'finished',slot:slot.slot_id,status:finished.status,
          validation:finished.validation,summary:finished.performance_summary}));
      }
    }
    const summary=ledger('status');
    console.log(JSON.stringify({event:'matrix_complete',summary}));
    if(summary.attempts.some(row=>row.state!=='completed'||row.finish?.validation?.valid!==true))process.exitCode=1;
  } finally {await browser.context.close();}
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const {values,positionals}=parseArgs({allowPositionals:true,options:Object.fromEntries(
    ['plan','disc','python','playwright','out','build','browser-profile','url'].map(k=>[k,{type:'string'}]))});
  try {
    const required=positionals[0]==='profile'?['out','build','browser-profile']:['plan','disc'];
    for(const key of required)if(!values[key])throw Error('Missing required --'+key+'; see docs/HITCH_CAPTURE.md');
    const pw=await playwright(values.playwright);
    if(positionals[0]==='profile')await profile(values,pw);
    else if(positionals[0]==='run')await run(values,pw);
    else throw Error('Expected profile or run; see docs/HITCH_CAPTURE.md');
  } catch(error) {console.error(error);process.exitCode=1;}
}
