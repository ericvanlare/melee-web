import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const runtime=fs.readFileSync(new URL('../web/runtime-development.mjs',import.meta.url),'utf8');
const code=runtime.slice(runtime.indexOf('function beginReplayPaintControl(){'),runtime.indexOf('\nconst log='));
assert(code.startsWith('function beginReplayPaintControl(){'));
function fixture(mode,enabled=true,drift=false){
  const classes=new Set();let reads=0,clock=10;
  const canvas={width:640,height:480,getBoundingClientRect(){
    return{x:20,y:100,width:900+(drift&&reads++?1:0),height:675};
  }};
  const scope={startupUrl:new URL('https://example.test/?hitch-ui-paint='+mode),
    hitchCaptureFromUrl:enabled,devicePixelRatio:2,performance:{now:()=>clock++},
    document:{documentElement:{classList:{add:x=>classes.add(x),remove:x=>classes.delete(x)}}},
    $:id=>{assert.equal(id,'canvas');return canvas;}};
  vm.createContext(scope);vm.runInContext(code,scope);
  return {scope,classes};
}
for(const mode of ['normal','hidden']){
  const f=fixture(mode);const control=f.scope.beginReplayPaintControl();
  assert.equal(f.classes.has('hitch-paint-hidden'),mode==='hidden');
  assert.equal(control.evidence.mode,mode);
  assert.equal(control.evidence.diagnostic_only,mode==='hidden');
  assert.deepEqual(control.evidence.geometry_before,control.evidence.geometry_after);
  control.restore();const end=control.evidence.ended_ms;control.restore();
  assert.equal(control.evidence.ended_ms,end);
  assert.equal(control.evidence.restored,true);
  assert.equal(f.classes.size,0);
}
assert.throws(()=>fixture('hidden',false).scope.beginReplayPaintControl(),/requires hitch capture/);
assert.throws(()=>fixture('unknown').scope.beginReplayPaintControl(),/Unknown/);
const drift=fixture('hidden',true,true);
assert.throws(()=>drift.scope.beginReplayPaintControl(),/changed canvas geometry/);
assert.equal(drift.classes.size,0,'Restore diagnostics even when setup fails');
console.log('Replay paint-control entry, geometry and restoration checks passed');
