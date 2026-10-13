import assert from 'node:assert/strict';
import test from 'node:test';
import {createBrowserDriver} from '../scripts/browser_driver.mjs';
const original={Module:globalThis.Module,document:globalThis.document,location:globalThis.location};
test.afterEach(()=>{for(const [name,value] of Object.entries(original)){
  if(value===undefined)delete globalThis[name];else globalThis[name]=value;
}});
function pageForFailure(){
  let evaluations=0;
  const node={textContent:'retained DOM evidence',dataset:{},disabled:false,getClientRects:()=>[{}]};
  globalThis.document={hidden:false,hasFocus:()=>true,getElementById:()=>node,querySelector:()=>node};
  globalThis.location={href:'http://127.0.0.1/fixture'};
  return {get evaluations(){return evaluations;},on(){},off(){},
    locator:()=>({focus:async()=>{}}),
    keyboard:{down:async()=>{throw Error('input transport failed while SSS may be yielded');},up:async()=>{}},
    evaluate:async(fn,arg)=>{++evaluations;return fn(arg);},waitForTimeout:async()=>{}};
}
for(const kind of ['false','missing','throwing','true','legacy']){
  test(`actual driver input-error diagnostics ${kind} guard preserves native entry policy`,async()=>{
    const calls=[];globalThis.Module={
      _melee_web_native_menu_phase:()=>{calls.push('phase');return 7;},
      _melee_web_native_menu_running:()=>{calls.push('running');return 1;},
    };
    if(kind!=='missing'&&kind!=='legacy')globalThis.Module.matchFrameEffectNativeIdle=()=>{
      calls.push('predicate');if(kind==='throwing')throw Error('predicate unavailable');return kind==='true';
    };
    const page=pageForFailure();const driver=createBrowserDriver(page,
      kind==='legacy'?{}:{nativeIdlePredicate:'matchFrameEffectNativeIdle'});
    await assert.rejects(driver.pressChord(['Enter'],{holdMs:120,releaseMs:220}),error=>{
      assert.equal(error.step,'keyboard-chord');assert.equal(error.diagnostics.status,'retained DOM evidence');
      if(kind==='true'||kind==='legacy'){
        assert.equal(error.diagnostics.phase,7);assert.equal(error.diagnostics.running,1);
      }else{
        assert.equal(error.diagnostics.phase,null);assert.equal(error.diagnostics.running,null);
        assert.equal(error.diagnostics.native_idle,false);
      }
      if(kind==='legacy')assert.equal(Object.hasOwn(error.diagnostics,'native_idle'),false);
      return true;
    });
    assert.equal(page.evaluations,1,'guard and native reads must share the one diagnostic evaluation');
    if(kind==='true')assert.deepEqual(calls,['predicate','phase','running']);
    else if(kind==='legacy')assert.deepEqual(calls,['phase','running']);
    else assert.equal(calls.some(x=>x==='phase'||x==='running'),false,
      'false, missing or throwing JS guard must prevent every raw native getter');
    driver.dispose();
  });
}
test('actual driver rejects malformed optional predicate before attaching listeners',()=>{
  const page={on(){throw Error('must not attach');}};
  assert.throws(()=>createBrowserDriver(page,{nativeIdlePredicate:'Module.bad.path'}),/predicate name/);
});
