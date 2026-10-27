"""Extracted development helper/constructor route with explicit owner/audio doubles.

Control evidence only: no browser, native constructor, timing or comparison claim.
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(os.environ.get('MELEE_PREPARED_LAUNCH_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'tests'))
from check_gameplay import node_runtime
from owned_test_workspace import OwnedWorkspaceTests


class PreparedConstructorLaunchControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, 'prepared-constructor-launch-')

    def test_actual_helper_and_constructor_route(self):
        candidate = Path(__file__).resolve().parents[1]
        development = (candidate / 'web/runtime-development.mjs').read_text()
        route = (candidate / 'tests/stadium_c1a_browser_test.mjs').read_text()
        helper = development[development.index('let firstCssConstructorLaunchAttempted='):development.index('window.meleeNetTimingPauseEligibilitySnapshot=')]
        route_start = route.index('  await timeout(page.evaluate(async generation => {', route.index('async function runFirstCssConstructorReturn('))
        route_end = route.index('  await timeout(page.waitForFunction(', route_start)
        route = route[route_start:route_end]
        import json
        script = r'''
import assert from 'node:assert/strict';
const helper=HELPER,route=ROUTE;
const make=new Function('hook',`return (async()=>{
 const events=[],queue=[];let launches=0,inBoundary=false;
 const state={state:'prepared',busy:false,loading:null,requiresReload:false,ready:true,canStart:true,phase:0,running:false};
 const observed={schema:'melee-web-stadium-first-css-constructor-return-observation-v2',mode:'constructor_return_only_v2',state:'armed',error:null,native_phase:0,running:false,host_entered:false,armed_owner_stable:true,armed_world_generation:17,constructor_return_terminal:false,source_steps:0,source_draws:0};
 const originalModule={_melee_web_native_menu_stadium_first_css_constructor_return_v2_arm(){},
 _melee_web_native_menu_stadium_first_css_draw_observe(){assert(inBoundary);events.push('observe');return 1;},
 UTF8ToString(){return JSON.stringify(observed);},_melee_web_native_menu_phase(){return state.phase;},
 _melee_web_native_menu_running(){return state.running?1:0;},
 _melee_web_native_menu_launch(){assert(inBoundary);events.push('nativeLaunch');launches++;return hook.launchResult??1;}};
 let Module=originalModule,owner={handle:{getState:()=>{assert(inBoundary);return {...state};}}};
 const originalOwner=owner;
 const check=result=>{if(result!==1)throw Error('native launch refused');return result;};
 const originalBoundary=run=>{events.push('queue');return new Promise((resolve,reject)=>queue.push({run,resolve,reject}));};
 let boundary=originalBoundary;
 const flush=()=>{while(queue.length){const c=queue.shift();try{inBoundary=true;c.resolve(c.run());}catch(e){c.reject(e);}finally{inBoundary=false;}}};
 const mutate=kind=>{if(kind==='owner')owner={handle:originalOwner.handle};
 else if(kind==='module')Module={...originalModule};else if(kind==='handle')owner.handle={getState:()=>{assert(inBoundary);return {...state};}};
 else if(kind==='boundary')boundary=run=>originalBoundary(run);
 else if(kind==='generation')observed.armed_world_generation++;
 else if(kind==='unload')state.state='idle';else if(kind==='destroy')state.requiresReload=true;
 else if(kind==='reprepare'){state.state='preparing';state.busy=true;}
 else if(kind==='ordinary')delete originalModule._melee_web_native_menu_stadium_first_css_constructor_return_v2_arm;
 else if(kind==='mode')observed.mode='other';else if(kind==='failed')observed.state='failed';
 else if(kind==='unstable')observed.armed_owner_stable=false;
 else if(kind==='running')state.running=true;else if(kind==='terminal')observed.constructor_return_terminal=true;};
 const audio=async label=>{events.push(label);if(hook.at===label)mutate(hook.change);if(hook.error===label)throw Error(label+' failed');};
 const prepareAudio=()=>audio('prepareAudio'),pauseAudioForPreparation=()=>audio('pauseAudio'),waitForAudioRender=()=>audio('waitForAudioRender');
 const resetTiming=()=>events.push('resetTiming'),syncAudio=()=>events.push('syncAudio');
 const $=()=>({focus:()=>events.push('focus')});let inputDirty=false;const window={};
 ${helper}
 if(hook.initial)mutate(hook.initial);
 const launch=window.meleeStadiumFirstCssConstructorReturnLaunch;
 let done=false,error=null;
 let task;
 if(hook.route){
  globalThis.meleeStadiumFirstCssConstructorReturnLaunch=launch;
  const page={evaluate:async(fn,arg)=>{events.push('routeEvaluate');return fn(arg);}};
  const timeout=p=>p,remaining=()=>5000,LIMITS={sourceTransitionMs:20000};
  const armed={observation:{armed_world_generation:17}};
  task=(async()=>{${route}})();
 }else task=launch(hook.generation??17);
 const settled=task.catch(e=>{error=e.message;}).finally(()=>{done=true;});
 if(hook.concurrent)await assertConcurrent();
 async function assertConcurrent(){try{await launch(17);throw Error('concurrent call accepted');}catch(e){if(!/already attempted/.test(e.message))throw e;}}
 for(let n=0;n<40&&!done;n++){
  await Promise.resolve();
  if(queue.length){if(hook.beforeFinal&&events.includes('waitForAudioRender')){mutate(hook.beforeFinal);hook.beforeFinal=null;}flush();}
 }
 await settled;
 let reused=false;try{await launch(17);}catch(e){reused=/already attempted/.test(e.message);}
 delete globalThis.meleeStadiumFirstCssConstructorReturnLaunch;
 return {events,launches,error,reused,inputDirty};
})()`);
let count=0;
async function control(options,success=false){const r=await make({...options});count++;
 assert.equal(r.launches,success||options.launchResult===0?1:0,JSON.stringify({options,r}));
 assert.equal(r.error===null,success,JSON.stringify({options,r}));assert(r.reused);
 if(success){assert(r.inputDirty);assert.deepEqual(r.events.filter(x=>['prepareAudio','pauseAudio','waitForAudioRender','nativeLaunch'].includes(x)),['prepareAudio','pauseAudio','waitForAudioRender','nativeLaunch']);
 assert.equal(r.events.filter(x=>x==='queue').length,2);assert(r.events.indexOf('nativeLaunch')>r.events.indexOf('waitForAudioRender'));}
 return r;}
await control({route:true,concurrent:true},true);
for(const initial of ['unload','destroy','reprepare','ordinary','mode','failed','unstable','running','terminal'])await control({initial});
await control({generation:18});await control({generation:0});
for(const at of ['prepareAudio','pauseAudio','waitForAudioRender']){
 for(const change of ['owner','module','handle','boundary','generation','unload','destroy','reprepare','failed','unstable','running','terminal'])await control({at,change});
 await control({error:at});
}
for(const beforeFinal of ['owner','module','handle','boundary','generation','unload','destroy','reprepare','failed','unstable','running','terminal'])await control({beforeFinal});
await control({launchResult:0});
console.log(JSON.stringify({schema:'stadium-prepared-constructor-launch-controls-v1',result:'PASS',controls:count,scope:'Extracted actual helper and constructor route; explicit queue/audio/native/owner doubles only.'}));
'''.replace('HELPER', json.dumps(helper)).replace('ROUTE', json.dumps(route))
        # new Function has no module lexical imports; expose only the assertion double.
        script = script.replace("const helper=", "globalThis.assert=assert;\nconst helper=", 1)
        script_path = self.scratch / 'controls.mjs'
        script_path.write_text(script)
        result = subprocess.run([str(node_runtime()), str(script_path)], capture_output=True, text=True, timeout=20)
        (self.scratch / 'execution.log').write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"result":"PASS"', result.stdout)
        print(result.stdout.strip())
