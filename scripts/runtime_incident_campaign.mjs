#!/usr/bin/env node
/**
 * Plan or run the bounded natural incident campaign.
 *
 * The default output is a frozen attempt plan. Pass --run to execute the four
 * attempts sequentially. Every attempt gets a fresh page, while the campaign
 * retains one external persistent browser profile so warm runs can actually
 * reuse the same origin cache. Cold attempts request the runtime's origin
 * render-cache clear. The browser driver cache remains uncontrolled and is
 * reported as such by the fixture.
 */
import fs from 'node:fs/promises';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {parseArgs} from 'node:util';

const {values}=parseArgs({options:{
  url:{type:'string'},disc:{type:'string'},'out-root':{type:'string'},
  playwright:{type:'string'},'build-dir':{type:'string'},contention:{type:'boolean'},
  run:{type:'boolean'},help:{type:'boolean'}}});
if(values.help){
  console.log('Usage: node scripts/runtime_incident_campaign.mjs --url URL --disc OWNED_CISO --out-root DIR [--playwright DIR] [--build-dir DIR] [--contention] [--run]');
  process.exit(0);
}
if(!values.url||!values.disc||!values['out-root'])
  throw Error('Use --url URL --disc OWNED_CISO --out-root DIR [--playwright DIR] [--build-dir DIR] [--contention] [--run]');
const baseUrl=new URL(values.url);
if(!['http:','https:'].includes(baseUrl.protocol)||!baseUrl.pathname.endsWith('/runtime.html'))
  throw Error('--url must be an HTTP(S) runtime.html URL');
const outputRoot=path.resolve(values['out-root']);
const harness=path.resolve(import.meta.dirname,'../tests/fighter_cpu9_lineup_browser_test.mjs');
const contention=values.contention===true;
const condition=contention?'controlled-contention':'shared-host-uncontrolled';
const browserProfile=path.join(outputRoot,'.campaign-browser-profile');
const planPath=path.join(outputRoot,'campaign-plan.json');
const receiptPath=path.join(outputRoot,'campaign-receipt.json');
const attempts=[
  {id:'A-final-destination-cold',lineup:'A',stage_kind:'final-destination',cache:'cold'},
  {id:'A-final-destination-warm',lineup:'A',stage_kind:'final-destination',cache:'warm'},
  {id:'B-battlefield-cold',lineup:'B',stage_kind:'battlefield',cache:'cold'},
  {id:'B-battlefield-warm',lineup:'B',stage_kind:'battlefield',cache:'warm'},
].map(attempt=>({...attempt,label:condition,matches:2,
  wall_bound_seconds:600,stop_on_timing_pause:true,audio:'enabled'}));
function attemptUrl(attempt){
  const url=new URL(baseUrl.href);
  if(attempt.cache==='cold')url.searchParams.set('render-cache','clear');
  return url.href;
}
function commandFor(attempt){
  const out=path.join(outputRoot,attempt.id,attempt.label);
  const args=[harness,'--url',attemptUrl(attempt),'--disc',values.disc,'--out',out,
    '--lineup',attempt.lineup,'--matches','2','--stage-kind',attempt.stage_kind,
    '--wall-bound-seconds','600','--stop-on-timing-pause','--user-data-dir',browserProfile];
  if(values.playwright)args.push('--playwright',path.resolve(values.playwright));
  if(values['build-dir'])args.push('--build-dir',path.resolve(values['build-dir']));
  if(contention)args.push('--controlled-contention');
  return {command:process.execPath,args,out};
}
const plan={schema:'melee-web-runtime-incident-campaign-v1',mode:values.run?'run':'plan-only',
  attempt_count:attempts.length,contention:condition,condition,
  plan_path:planPath,receipt_path:receiptPath,
  browser_context:{kind:'persistent-user-data-dir',reuse:'one external profile retained across sequential attempts',
    profile_cleanup:'removed only after a successful --run; preserved after failure',driver_cache:'uncontrolled'},
  browser_cache:'cold attempts request origin render-cache clear; warm attempts reuse the campaign persistent profile; actual native warmth requires harness cache_evidence.restore_observed; driver cache uncontrolled',
  reducer_packet:{schema:'melee-web-runtime-incident-reducer-v1',boundary:'natural incident timing pause',
    experiments_per_boundary:2,stop_on_first_unexpected_pause:true,
    preserve:'actual pause receipt, screenshots, scalar callback capture'},
  attempts:attempts.map(attempt=>({...attempt,url:attemptUrl(attempt),...commandFor(attempt)}))};
await fs.mkdir(outputRoot,{recursive:true});
await fs.writeFile(planPath,JSON.stringify(plan,null,2)+'\n',{flag:'wx'});
if(!values.run){console.log(JSON.stringify(plan,null,2));process.exit(0);}

const receipt={schema:'melee-web-runtime-incident-campaign-receipt-v1',status:'running',
  plan_path:planPath,condition,browser_context:{kind:'persistent-user-data-dir',profile_path:browserProfile,profile_cleanup:'pending',
    driver_cache:'uncontrolled'},attempts:[],stop:{reason:null,attempt:null}};
const writeReceipt=()=>fs.writeFile(receiptPath,JSON.stringify(receipt,null,2)+'\n');
await writeReceipt();
let campaignExitCode=0;
try{
  for(const attempt of plan.attempts){
    await fs.mkdir(path.dirname(attempt.out),{recursive:true});
    const started={id:attempt.id,label:attempt.label,cache:attempt.cache,status:'running'};
    receipt.attempts.push(started); await writeReceipt();
    console.log(JSON.stringify({event:'attempt-start',id:attempt.id,label:attempt.label,cache:attempt.cache}));
    const exitCode=await new Promise((resolve,reject)=>{
      const child=spawn(attempt.command,attempt.args,{stdio:'inherit'});
      child.once('error',reject);
      child.once('exit',(code,signal)=>resolve(code??(signal?1:0)));
    });
    started.status=exitCode===0?'pass':'fail'; started.exit_code=exitCode;
    if(exitCode!==0){
      console.error(JSON.stringify({event:'attempt-failed',id:attempt.id,exit_code:exitCode}));
      receipt.status='failed'; receipt.stop={reason:'attempt-failed',attempt:attempt.id};
      campaignExitCode=exitCode||1; await writeReceipt(); break;
    }
    console.log(JSON.stringify({event:'attempt-complete',id:attempt.id}));
    await writeReceipt();
  }
  if(campaignExitCode===0){receipt.status='pass';receipt.stop={reason:'completed',attempt:null};}
}catch(error){
  receipt.status='failed';receipt.stop={reason:'runner-error',attempt:receipt.attempts.at(-1)?.id??null,error:error.message};
  campaignExitCode=1;
}finally{
  if(receipt.status==='pass'){
    await fs.rm(browserProfile,{recursive:true,force:true});
    receipt.browser_context.profile_cleanup='removed';
  }else receipt.browser_context.profile_cleanup='preserved-after-failure';
  receipt.finished_at=new Date().toISOString();
  await writeReceipt();
}
if(campaignExitCode!==0)process.exitCode=campaignExitCode;
