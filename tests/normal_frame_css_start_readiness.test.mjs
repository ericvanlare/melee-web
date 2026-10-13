import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {CSS_SSS_TRANSITION_LIMITS} from './stadium_c1a_css_sss_reducer.mjs';
const source = await fs.readFile(new URL('./vs_rules_item_menu_browser_test.mjs', import.meta.url), 'utf8');
const start = source.indexOf('async function readNormalObservationCssStart(');
const middle = source.indexOf('async function startNormalObservationCssWhenReady(', start);
const end = source.indexOf('const configureCompetitiveSecondHuman =', middle);
assert(start >= 0 && middle > start && end > middle);
const readerSource = source.slice(start, middle), startSource = source.slice(middle, end);
function sample(index = 0, cooldown = 0) {
  const ids = [0,-1,8,1,-1,cooldown,4,0,2,0,100+index,0,0,0];
  return {native_idle:true,phase:1,running:1,css:{ids,geometry:Array(8).fill(0)},
    input:{ready:1,focused:1,visible:1,active:1,keyboard_layout:0,
      keyboard_requested_mask:3,keyboard_active_mask:3,physical_mask:0,samples:100+index,
      pads:[{source:'keyboard',err:0,buttons:0},{source:'keyboard',err:0,buttons:0}]}};
}
function makeStart(samples, {transitionFails = false} = {}) {
  let time = 0, index = 0;
  const calls = [], persisted = [];
  const context = vm.createContext({
    CSS_SSS_TRANSITION_LIMITS, Date:class extends Date {static now(){return time;}},
    report:{competitiveMatchStart:{},input:[],timingPauses:[]},frameEffectNativeCallsForbidden:false,
    page:{locator:()=>({focus:async()=>calls.push('focus')}),
      waitForTimeout:async ms=>{time+=ms;calls.push('wait');}},
    resumeTimingPause:async()=>{calls.push('raw-legacy');throw Error('forbidden raw helper');}, ensureNoError:async()=>{calls.push('raw-legacy');throw Error('forbidden raw helper');},
    readNormalObservationCssStart:async()=>{calls.push('sample');return structuredClone(samples[Math.min(index++,samples.length-1)]);},
    persistReport:async()=>persisted.push(JSON.parse(JSON.stringify(context.report))),
    driver:{pressChord:async(keys,options)=>{assert.deepEqual(Array.from(keys),['Enter']);assert.equal(options.holdMs,120);assert.equal(options.releaseMs,220);calls.push('Enter');time+=340;}},
    waitPhase:async(phase,label,remaining)=>{
      calls.push(['phase',phase,remaining]);assert(remaining<=90000 && remaining>0);
      if(transitionFails)throw Error('original SSS transition refused');
    },
  });
  vm.runInContext(startSource,context);
  return {context,calls,persisted};
}
test('actual Start function waits for late source readiness then sends exactly one original Enter',async()=>{
  const first=sample(0,10);first.css.ids[8]=0;
  const post=sample(3);post.phase=3;post.css=null;
  const {context,calls,persisted}=makeStart([first,sample(1,4),sample(2),post]);
  await context.startNormalObservationCssWhenReady();
  const e=context.report.competitiveMatchStart.css_start_transition;
  assert.equal(e.samples.length,3);assert.equal(e.readiness.css.ids[5],0);
  assert.equal(e.start_attempts,1);assert.equal(e.status,'sss_entered');
  assert.equal(calls.filter(x=>x==='Enter').length,1);
  assert(calls.indexOf('Enter')>calls.lastIndexOf('wait'));
  assert.equal(persisted[0].competitiveMatchStart.css_start_transition.start_attempts,0);
  assert.equal(e.transition_remaining_ms_at_start,89620,'readiness consumes the original transition budget');
});
test('actual readiness refuses wrong two-keyboard shape without a Start edge',async()=>{
  const rows=Array.from({length:181},(_,i)=>{const s=sample(i);s.input.keyboard_active_mask=1;return s;});
  const {context,calls,persisted}=makeStart(rows);
  await assert.rejects(context.startNormalObservationCssWhenReady(),/bounded deadline|source-callback bound/);
  assert(!calls.includes('Enter'));assert.equal(context.report.competitiveMatchStart.css_start_transition.start_attempts,0);
  assert(persisted.length>0);
});
test('actual readiness timeout and missing live source observation refuse before Start',async()=>{
  for(const row of [sample(),{native_idle:false},{native_idle:true,phase:1,running:1,css:null}]){
    const {context,calls}=makeStart([row]);
    await assert.rejects(context.startNormalObservationCssWhenReady());
    assert(!calls.includes('Enter'));assert(context.report.competitiveMatchStart.css_start_transition.failure);
  }
});
test('actual Start transition failure retains post-input source evidence and never retries',async()=>{
  const post=sample(2);post.css.ids[11]=0x1000;post.css.ids[12]=2;post.dom={error:'original SSS transition refused'};
  const {context,calls,persisted}=makeStart([sample(0),sample(1),post],{transitionFails:true});
  await assert.rejects(context.startNormalObservationCssWhenReady(),/original SSS transition refused/);
  const e=context.report.competitiveMatchStart.css_start_transition;
  assert.equal(e.post_input.css.ids[11],0x1000);assert.equal(e.final.css.ids[11],0x1000);
  assert.equal(calls.filter(x=>x==='Enter').length,1);assert(e.failure);assert(persisted.length>=3);
});
function makeReader({idle=true,refused=false,never=false}={}){
  const calls=[];const row=sample();const HEAP32=new Int32Array(128),HEAPF32=new Float32Array(128);
  HEAP32.set(row.css.ids,16);
  let allocations=0;
  const Module={_melee_web_native_menu_message:()=>'',matchFrameEffectNativeIdle:()=>idle,
    _melee_web_native_menu_phase:()=>{calls.push('phase');return 1;},
    _melee_web_native_menu_running:()=>1,_melee_web_input_message:()=>JSON.stringify(row.input),UTF8ToString:x=>x,
    _malloc:n=>{calls.push(['malloc',n]);return ++allocations===1?64:128;},
    _free:p=>calls.push(['free',p]),HEAP32,HEAPF32,
    _melee_web_css_observe_port:(port,kind)=>{calls.push(['observe',port,kind]);return refused?0:1;}};
  const context=vm.createContext({Module,document:{hidden:false,hasFocus:()=>true,querySelector:()=>null},setTimeout,clearTimeout,frameEffectNativeCallsForbidden:false,
    page:{evaluate:async fn=>{calls.push('evaluate');return never?new Promise(()=>{}):fn();}}});
  vm.runInContext(readerSource,context);
  return {context,calls};
}
test('actual observer reader retains all14source ids and frees both temporary outputs',async()=>{
  const {context,calls}=makeReader();const row=await context.readNormalObservationCssStart();
  assert.equal(row.css.ids.length,14);assert.equal(row.css.ids[10],100);
  assert.deepEqual(calls.filter(x=>Array.isArray(x)),[['malloc',56],['malloc',32],['observe',0,8],['free',64],['free',128]]);
});
test('actual observer reader refuses missing idle without entering native and closes access on refusal',async()=>{
  const {context,calls}=makeReader({idle:false});const row=await context.readNormalObservationCssStart();
  assert.equal(row.native_idle,false);assert.deepEqual(calls,['evaluate']);
  const refused=makeReader({refused:true});await assert.rejects(refused.context.readNormalObservationCssStart(),/observer refused/);
  assert.equal(refused.context.frameEffectNativeCallsForbidden,true);
});
test('actual observer read transport timeout is bounded and forbids ordinary native cleanup',async()=>{
  const {context,calls}=makeReader({never:true});
  await assert.rejects(context.readNormalObservationCssStart(5),/transport deadline/);
  assert.equal(context.frameEffectNativeCallsForbidden,true);assert.deepEqual(calls,['evaluate']);
});


test('actual Start catch never repeats a native read after post-input transport uncertainty',async()=>{
  const {context,calls}=makeStart([sample(0),sample(1)]);
  let reads=0;
  context.readNormalObservationCssStart=async()=>{
    ++reads;
    if(reads<3)return sample(reads);
    context.frameEffectNativeCallsForbidden=true;
    throw Error('post-input read response lost');
  };
  await assert.rejects(context.startNormalObservationCssWhenReady(),/response lost/);
  assert.equal(reads,3,'uncertain post-input read must not dispatch a final native read');
  assert.equal(calls.filter(x=>x==='Enter').length,1);
});


test('actual readiness false-idle then late-idle uses no legacy native helper before one Start',async()=>{
  const post=sample(3);post.phase=3;post.css=null;
  const {context,calls}=makeStart([{native_idle:false,dom:{}},sample(0),sample(1),post]);
  await context.startNormalObservationCssWhenReady();
  assert.equal(context.frameEffectNativeCallsForbidden,false);
  assert.equal(calls.filter(x=>x==='Enter').length,1);assert(!calls.includes('raw-legacy'));
});
test('actual readiness false-idle deadline stays browser-only with zero raw helpers/Start',async()=>{
  const {context,calls,persisted}=makeStart([{native_idle:false,dom:{}}]);
  await assert.rejects(context.startNormalObservationCssWhenReady(),/bounded deadline/);
  assert.equal(context.frameEffectNativeCallsForbidden,true);
  assert(!calls.includes('Enter'));assert(!calls.includes('raw-legacy'));assert(persisted.length>0);
});
test('actual guarded readiness refuses timing pause without Resume or Start',async()=>{
  const paused=sample();paused.message='Paused after a timing disruption: source clock';
  const {context,calls}=makeStart([paused]);
  await assert.rejects(context.startNormalObservationCssWhenReady(),/no Resume/);
  assert.equal(context.report.timingPauses[0].status,'failed_route');
  assert(!calls.includes('Enter'));assert(!calls.includes('raw-legacy'));
});


test('actual guarded post-Start false-idle then late-idle never falls through to raw legacy polling',async()=>{
  const post=sample(3);post.phase=3;post.css=null;
  const {context,calls}=makeStart([sample(0),sample(1),{native_idle:false,dom:{}},post]);
  const read=context.readNormalObservationCssStart;
  context.readNormalObservationCssStart=async()=>{
    const row=await read();context.frameEffectNativeCallsForbidden=row.native_idle!==true;return row;
  };
  await context.startNormalObservationCssWhenReady();
  const e=context.report.competitiveMatchStart.css_start_transition;
  assert.equal(e.post_input.native_idle,false);assert.equal(e.transition_last.phase,3);
  assert.equal(e.transition_observations,2);assert.equal(context.frameEffectNativeCallsForbidden,false);
  assert.equal(calls.filter(x=>x==='Enter').length,1);assert(!calls.includes('raw-legacy'));
  assert(!calls.some(x=>Array.isArray(x)&&x[0]==='phase'),'legacy waitPhase must not be called');
});
