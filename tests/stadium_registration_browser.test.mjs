import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {armStadiumRegistrationInPage, collectStadiumRegistrationInPage, validateStadiumRegistrationSnapshot} from '../scripts/stadium_registration_browser.mjs';

function status(overrides={}) {
  return {schema:'melee-web-source-registration-status-v1',armed:true,sealed:false,invalid:false,
    overflow:false,active_world:'0',worlds:0,count:0,pending_setup:false,
    row_capacity:65536,slot_capacity:8192,storage_bytes:10000000,...overrides};
}
const row=i=>JSON.stringify({schema:'melee-web-source-registration-row-v1',sequence:i,kind:i+1});
function runtime(s,rows=[]) {
  const calls=[];
  return {calls,Module:{UTF8ToString:x=>x,stadiumMatchedSetupNativeIdle:()=>true,
    _melee_web_stadium_registration_arm:()=>{calls.push('arm');return 1;},
    _melee_web_stadium_registration_seal:()=>{calls.push('seal');s.sealed=true;return 1;},
    _melee_web_stadium_registration_status_json:()=>JSON.stringify(s),
    _melee_web_stadium_registration_read_json:i=>rows[i]}};
}
const invoke=(fn,context,arg)=>vm.runInNewContext('('+fn.toString()+')(arg)',{...context,arg});

test('arm executes the actual page adapter and refuses missing, active or dirty observers',()=>{
  const r=runtime(status());assert.equal(invoke(armStadiumRegistrationInPage,r).count,0);
  assert.deepEqual(r.calls,['arm']);
  assert.throws(()=>invoke(armStadiumRegistrationInPage,{}),/unavailable/);
  for(const change of [{active_world:'3'},{worlds:1},{count:1},{overflow:true},{invalid:true}])
    assert.throws(()=>invoke(armStadiumRegistrationInPage,runtime(status(change))),/not empty and healthy/);
  r.Module._melee_web_stadium_registration_arm=()=>0;
  assert.throws(()=>invoke(armStadiumRegistrationInPage,r),/before the first source world/);
  for(const idle of [()=>false,undefined]){
    const busy=runtime(status());busy.Module.stadiumMatchedSetupNativeIdle=idle;
    for(const key of ['_melee_web_stadium_registration_arm','_melee_web_stadium_registration_seal','_melee_web_stadium_registration_status_json','_melee_web_stadium_registration_read_json'])
      busy.Module[key]=()=>assert.fail('busy native export called');
    assert.throws(()=>invoke(armStadiumRegistrationInPage,busy),/no native reads attempted/);
    assert.throws(()=>invoke(collectStadiumRegistrationInPage,busy,{seal:false}),/no native reads attempted/);
    assert.throws(()=>invoke(collectStadiumRegistrationInPage,busy,{seal:true}),/no native reads attempted/);
  }
});

test('raw export seals once and preserves row strings; failure retains an unsealed snapshot',()=>{
  const rows=[row(0),row(1)],r=runtime(status({count:2,worlds:4}),rows);
  const result=invoke(collectStadiumRegistrationInPage,r,{seal:true});
  assert.deepEqual([...result.rows],rows);assert.deepEqual(r.calls,['seal']);
  const checked=validateStadiumRegistrationSnapshot(result);
  assert.equal(checked.complete,true);assert.equal(checked.original_lifecycle_parity,false);assert.equal(checked.C3,false);
  const failed=runtime(status({count:2,worlds:1,invalid:true,active_world:'3'}),rows);
  const partial=invoke(collectStadiumRegistrationInPage,failed,{seal:false});
  assert.deepEqual([...partial.rows],rows);assert.deepEqual(failed.calls,[]);
  assert.throws(()=>validateStadiumRegistrationSnapshot(partial),/incomplete or unhealthy/);
  r.Module._melee_web_stadium_registration_seal=()=>0;
  assert.throws(()=>invoke(collectStadiumRegistrationInPage,r,{seal:true}),/incomplete source lifetime/);
});

test('bounded framing refuses holes, overflow, unsealed data and sequence gaps',()=>{
  assert.throws(()=>invoke(collectStadiumRegistrationInPage,runtime(status({count:1})),{seal:false}),/missing/);
  assert.throws(()=>invoke(collectStadiumRegistrationInPage,runtime(status({count:65537})),{seal:false}),/fixed bound/);
  const s=status({sealed:true,worlds:4,count:2});
  for(const change of [{sealed:false},{invalid:true},{overflow:true},{active_world:'7'},{pending_setup:true},{count:3},{row_capacity:65537}])
    assert.throws(()=>validateStadiumRegistrationSnapshot({status:{...s,...change},rows:[row(0),row(1)]}),/incomplete or unhealthy/);
  assert.throws(()=>validateStadiumRegistrationSnapshot({status:s,rows:[row(0),row(0)]}),/framing differs/);
});

test('actual full-route phase exports only after ordinary retirement and cannot pass rejected registration',async()=>{
  const source=fs.readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
  const start=source.indexOf('async function pairedFullPhase(){'),end=source.indexOf('\n}',start);
  assert.ok(start>=0&&end>start);
  async function run(invalid=false){
    const events=[],rows=[row(0),row(1)],native=runtime(status({count:2,worlds:4,invalid}),rows);
    let observations=0;
    const ctx={Date,Promise,setTimeout,replayTimeoutMs:1000,replayPollMs:1,output:'/owned',pairedRegistration:true,
      fs:{writeFile:async(p,raw)=>events.push(['raw',p,raw])},path:{join:(...p)=>p.join('/')},
      installPairedFullLaunch:()=>{},installPairedFullObserver:()=>{},
      collectStadiumRegistrationInPage,validateStadiumRegistrationSnapshot,
      write:async(name)=>events.push(['write',name]),observePageOperation:async(label,fn)=>fn(),
      page:{evaluate:async(fn,arg)=>fn===collectStadiumRegistrationInPage?invoke(fn,native,arg):true,
        locator:()=>({click:async()=>events.push(['start'])})},
      report:{paired_v11_full_pair:{}},pageErrors:[],drainPairedFull:async()=>{},
      readPairedFirstGoStatus:async()=>{
        if(++observations===1)return {busy:false,status:{schema:'melee-web-stadium-paired-v11-full-pair-v1',whole_session_complete:true,failed:false}};
        assert.deepEqual(native.calls,[]);events.push(['retired']);
        return {busy:false,status:{native_retired:true},phase:0,running:0,runtime:{replay_report:{complete:true,pass:true,failures:[]}}};},
      pairedFullExpected:{total:10},pairedFullComparison:{finish:()=>({pass:true})},
      runPairedFullRngComparison:()=>({status:'PASS_SELECTED_RNG_COMPARISON'})};
    vm.createContext(ctx);vm.runInContext(source.slice(start,end+2),ctx);
    if(invalid){await assert.rejects(ctx.pairedFullPhase(),/incomplete or unhealthy/);assert.notEqual(ctx.report.paired_v11_full_pair.component_pass,true);}
    else{await ctx.pairedFullPhase();assert.equal(ctx.report.paired_v11_full_pair.component_pass,true);assert.equal(ctx.report.stadium_registration.C3,false);}
    assert.deepEqual(native.calls,['seal']);
    const raw=events.find(e=>e[0]==='raw'&&e[1].endsWith('stadium-registration-rows.jsonl'));
    assert.equal(raw[2],rows.join('\n')+'\n');assert.ok(events.indexOf(raw)>events.findIndex(e=>e[0]==='retired'));
  }
  await run();await run(true);
});
