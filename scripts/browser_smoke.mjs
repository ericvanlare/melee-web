#!/usr/bin/env node
/** Short real-browser readiness/lifecycle probe before an expensive replay.
 * Ordinary controls only. No timing, retail equivalence or content admission.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {createBrowserDriver} from './browser_driver.mjs';
import {loadBrowserTools} from './browser_tools.mjs';

const {values}=parseArgs({options:{
  ...Object.fromEntries(['url','surface','disc','out','playwright','timeout'].map(name=>[name,{type:'string'}])),
  help:{type:'boolean'},
}});
if(values.help){
  console.log('Usage: node scripts/browser_smoke.mjs --url HTTP_URL --surface development|public --out NEW_DIRECTORY [--disc OWNED_DISC] [--playwright PACKAGE_DIR] [--timeout 90000]');
  process.exit(0);
}
if(!values.url||!values.out||!['development','public'].includes(values.surface))throw Error('Expected --url, --surface development|public and --out; see --help');
if(!['http:','https:'].includes(new URL(values.url).protocol))throw Error('Use a real HTTP server');
const timeout=Number(values.timeout||90000);
if(!Number.isInteger(timeout)||timeout<1000||timeout>300000)throw Error('Timeout must be 1000..300000 ms');
const {chromium,browser:launchOptions}=await loadBrowserTools(values.playwright);
await fs.mkdir(path.dirname(path.resolve(values.out)),{recursive:true});
await fs.mkdir(values.out,{recursive:false});
const report={schema:'melee-web-browser-smoke-v1',scope:'Readiness and optional owned-disc import, original CSS entry and teardown only. Not replay, performance, retail comparison or complete gameplay acceptance.',
  url:values.url,surface:values.surface,started_at:new Date().toISOString(),checks:[],
  build_identity:'Not established by this probe; use the existing frozen-build/HTTP audit for acceptance.'};
let browser,driver;
try {
  browser=await chromium.launch({...launchOptions,headless:false,chromiumSandbox:true,timeout});
  report.browser=browser.version();
  const page=await browser.newPage({viewport:{width:1280,height:960}});
  driver=createBrowserDriver(page,{surface:values.surface,timeoutMs:timeout,deadline:Date.now()+timeout});
  const response=await page.goto(values.url,{timeout});
  if(response?.status()!==200)throw Error('HTTP startup failed: '+response?.status());
  if(!await page.evaluate(()=>crossOriginIsolated&&!!navigator.gpu))throw Error('Cross-origin isolation and WebGPU are required');
  await driver.waitForImport();report.checks.push('import control ready');
  if(values.disc){
    await driver.selectDisc(path.resolve(values.disc));await driver.waitForStart();
    report.checks.push('owned disc prepared');
    await driver.launch();report.checks.push('original CSS running');
    await driver.unload();report.checks.push('teardown and import control ready');
  }
  report.state=await driver.diagnostics();
  if(report.state.unavailable||report.state.error)throw Error(report.state.unavailable||report.state.error);
  if(report.state.errors?.length)throw Error('Browser errors retained');
  report.result='pass';
} catch(error){
  report.result='fail';report.failure=String(error.message||error);
  report.step=error.step||null;report.state=error.diagnostics||await driver?.diagnostics();
  process.exitCode=1;
} finally {
  driver?.dispose();
  try {await browser?.close();}catch(error){report.cleanup_error=String(error);report.result='fail';process.exitCode=1;}
  report.finished_at=new Date().toISOString();
  await fs.writeFile(path.join(values.out,'report.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
}
console.log(JSON.stringify({result:report.result,checks:report.checks,
  ...(report.failure?{step:report.step,failure:report.failure}:{}),
  report:path.join(values.out,'report.json')}));
