import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {armStadiumRegistrationInPage, collectStadiumRegistrationInPage, validateStadiumRegistrationSnapshot} from '../scripts/stadium_registration_browser.mjs';
import {derivePairedFull} from '../scripts/stadium_paired_v11_full.mjs';

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

test('registration projection admits exact live or offline provenance and joins every raw producer',()=>{
  const primary='a'.repeat(64),allocation='b'.repeat(64),input='c'.repeat(64),recipe='d'.repeat(64);
  const p={schema:'melee-web-stadium-paired-window-projection-v1',capture_profile:'paired_idle_registration_v1',
    source_schedule_verified:true,recipe_sha256:recipe,actual_frame_count:2,
    source_files:{'native-primary.mwro':{sha256:primary},'native-allocations.jsonl':{sha256:allocation},'native-inputs.mwri':{sha256:input}},
    matches:[0,1].map(i=>({match_index:i,window:{state_samples:3501},
      setup:{declared_setup:{stage:3,players:[0,1].map(()=>({character_kind:8,player_type:0,stocks:4}))}},
      actions:[{action_index:0,kind:'tick',source_tick:0,draw_ordinal:0}],
      states:Array.from({length:3501},(_,t)=>({selected:true,tick:{source_tick:t}}))}))};
  const e={result:'pass',scope:'original-stadium-paired-registration-v1',cleanup_errors:[],
    input_stream:{complete:true,sha256:input},native:{primary_sha256:primary,native_allocations_sha256:allocation,
      original_rng_scene_boundaries:{observed:true,sample_count:8,samples:Array.from({length:8},(_,i)=>({
        lifetime:Math.floor(i/4)+1,function:i%4<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit',phase:i%2?'return':'enter',value:1}))}}};
  assert.equal(derivePairedFull(p,recipe,2,e).matches.length,2);
  for(const alter of [x=>x.result='fail',x=>x.scope='original-stadium-paired-idle-scene-endpoints-v1',
    x=>x.cleanup_errors=['owned process remained'],x=>x.input_stream.complete=false,
    x=>x.input_stream.sha256='e'.repeat(64),x=>x.native.native_allocations_sha256='e'.repeat(64)]){
    const bad=structuredClone(e);alter(bad);assert.throws(()=>derivePairedFull(p,recipe,2,bad));
  }
  assert.throws(()=>derivePairedFull({...p,capture_profile:'unknown'},recipe,2,e),/unknown original capture profile/);
  const legacy=structuredClone(p);delete legacy.capture_profile;
  assert.throws(()=>derivePairedFull(legacy,recipe,2,e),/original endpoint postflight/);
  {
  // Modeled native leaves/reference dimensions; actual derivePairedFull performs admission.
  const recipe='d'.repeat(64),scope='original-paired-registration-offline-postflight-v1';
  const names=['report.json','launch.json','native-primary.mwro','native-primary-status.json',
    'native-inputs.mwri','native-input-status.json','native-allocations.jsonl','allocation-profile.json',
    'stadium-whole-paired-compact-rng.mwrl','cleanup.json','input-intentions.jsonl'];
  const source=Object.fromEntries(names.map((name,i)=>[name,{bytes:i+1,sha256:(i+1).toString(16).repeat(64)}]));
  const p={schema:'melee-web-stadium-paired-window-projection-v1',capture_profile:'paired_idle_registration_v1',
    source_schedule_verified:true,recipe_sha256:recipe,actual_frame_count:2,postflight_scope:scope,original_live_result:'fail',
    source_files:source,matches:[0,1].map(i=>({match_index:i,window:{state_samples:3501},
      setup:{declared_setup:{stage:3,players:[0,1].map(()=>({character_kind:8,player_type:0,stocks:4}))}},
      actions:[{action_index:0,kind:'tick',source_tick:0,draw_ordinal:0}],
      states:Array.from({length:3501},(_,t)=>({selected:true,tick:{source_tick:t}}))}))};
  const e={scope,result:'PASS_OFFLINE_POSTFLIGHT_REFERENCE_ONLY',original_live_result:'fail',inputs_unchanged:true,
    native_capture_repeated:false,original_operation_deadline_reclassified:false,browser_parity:false,C3:false,
    native_pid_absent:true,native_group_absent:true,inputs:structuredClone(source),
    input_stream:{complete:true,sha256:source['native-inputs.mwri'].sha256},
    native:{primary_sha256:source['native-primary.mwro'].sha256,native_allocations_sha256:source['native-allocations.jsonl'].sha256,
      original_rng_scene_boundaries:{observed:true,sample_count:8,samples:Array.from({length:8},(_,i)=>({
        lifetime:Math.floor(i/4)+1,function:i%4<2?'gm_Scene_Vs_OnEnter':'gm_Scene_Vs_OnExit',phase:i%2?'return':'enter',value:1}))}}};
  assert.equal(derivePairedFull(p,recipe,2,e).matches.length,2);
  for(const [key,value]of [['scope','original-paired-idle-offline-postflight-v1'],['result','pass'],
    ['original_live_result','pass'],['inputs_unchanged',false],['native_capture_repeated',true],
    ['original_operation_deadline_reclassified',true],['browser_parity',true],['C3',true],
    ['native_pid_absent',false],['native_group_absent',false]]){
    const bad=structuredClone(e);bad[key]=value;assert.throws(()=>derivePairedFull(p,recipe,2,bad),key);
    delete bad[key];assert.throws(()=>derivePairedFull(p,recipe,2,bad),'missing '+key);
  }
  for(const name of names){
    for(const mutate of [x=>delete x.inputs[name],x=>x.inputs[name].sha256='f'.repeat(64),x=>++x.inputs[name].bytes]){
      const bad=structuredClone(e);mutate(bad);assert.throws(()=>derivePairedFull(p,recipe,2,bad),name);
    }
    const bad=structuredClone(p);delete bad.source_files[name];assert.throws(()=>derivePairedFull(bad,recipe,2,e),name);
  }
  for(const [key,value]of [['capture_profile','unknown'],['postflight_scope','original-stadium-paired-registration-v1'],['original_live_result','pass']]){
    const bad=structuredClone(p);bad[key]=value;assert.throws(()=>derivePairedFull(bad,recipe,2,e),key);
  }
  const incomplete=structuredClone(e);incomplete.input_stream.complete=false;
  assert.throws(()=>derivePairedFull(p,recipe,2,incomplete),/literal input/);
  const foreign=structuredClone(e);foreign.native.primary_sha256='f'.repeat(64);
  assert.throws(()=>derivePairedFull(p,recipe,2,foreign),/primary producer/);
  const legacy=structuredClone(p);delete legacy.capture_profile;
  const oldReceipt=structuredClone(e);oldReceipt.scope='original-paired-idle-offline-postflight-v1';
  assert.equal(derivePairedFull(legacy,recipe,2,oldReceipt).matches.length,2); // Historical admission is unchanged.
  }
  const projectionPath=process.env.MELEE_PAIRED_REGISTRATION_PROJECTION;
  const receiptPath=process.env.MELEE_PAIRED_REGISTRATION_POSTFLIGHT;
  if(projectionPath||receiptPath){
    assert.ok(projectionPath&&receiptPath,'Both retained registration operands must be configured');
    const actual=JSON.parse(fs.readFileSync(projectionPath,'utf8'));
    const receipt=JSON.parse(fs.readFileSync(receiptPath,'utf8'));
    assert.equal(derivePairedFull(actual,actual.recipe_sha256,actual.actual_frame_count,receipt).matches.length,2);
    const mixed=structuredClone(receipt);mixed.inputs['native-inputs.mwri'].sha256='0'.repeat(64);
    assert.throws(()=>derivePairedFull(actual,actual.recipe_sha256,actual.actual_frame_count,mixed),/raw role/);
  }
});

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
