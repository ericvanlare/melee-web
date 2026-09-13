import assert from 'node:assert/strict';
import path from 'node:path';
import crypto from 'node:crypto';
import {BUILD_ARTIFACTS,HARNESS_ARTIFACTS,validateFrozenBuildProfile,
  verifyServedArtifacts} from '../scripts/run_hitch_matrix.mjs';

const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const artifacts=Object.fromEntries(BUILD_ARTIFACTS.map(name=>[name,hash(name)]));
const profile={schema:'melee-web-hitch-browser-profile',version:1,artifacts,
  harness_artifacts:Object.fromEntries(HARNESS_ARTIFACTS.map(name=>[name,hash(name)]))};
const manifest=BUILD_ARTIFACTS.map(name=>({path:path.resolve('frozen-build',name),sha256:artifacts[name]}));
assert.equal(BUILD_ARTIFACTS.length,14);
assert.equal(new Set(BUILD_ARTIFACTS).size,14);
assert(HARNESS_ARTIFACTS.includes('tools/browser_build_artifacts.json'));
validateFrozenBuildProfile(profile,manifest);
validateFrozenBuildProfile(profile,[...manifest].reverse());

for(const value of [null,{},[],{...profile,schema:'other'},{...profile,version:2}])
  assert.throws(()=>validateFrozenBuildProfile(value,manifest),/profile schema/);
for(const key of ['artifacts','harness_artifacts']) {
  for(const value of [undefined,null,{},[],{...profile[key],'unexpected.js':'a'.repeat(64)}])
    assert.throws(()=>validateFrozenBuildProfile({...profile,[key]:value},manifest),/inventory/);
  for(const name of Object.keys(profile[key])) {
    const incomplete={...profile[key]};delete incomplete[name];
    assert.throws(()=>validateFrozenBuildProfile({...profile,[key]:incomplete},manifest),/inventory/);
    for(const digest of [null,0,'','a'.repeat(63),'x'.repeat(64)])
      assert.throws(()=>validateFrozenBuildProfile({...profile,[key]:{...profile[key],[name]:digest}},manifest),/SHA-256/);
  }
}
for(const value of [undefined,null,{},[],manifest.slice(1),[...manifest,manifest[0]]])
  assert.throws(()=>validateFrozenBuildProfile(profile,value),/manifest inventory/);
for(const identity of [null,{}, {...manifest[0],path:7},
  {...manifest[0],path:'relative.wasm'}, {...manifest[0],sha256:null}])
  assert.throws(()=>validateFrozenBuildProfile(profile,[identity,...manifest.slice(1)]),/manifest identity/);
assert.throws(()=>validateFrozenBuildProfile(profile,[manifest[1],...manifest.slice(1)]),/Duplicate/);
assert.throws(()=>validateFrozenBuildProfile(profile,
  [{...manifest[0],path:path.resolve('frozen-build','unlisted.wasm')},...manifest.slice(1)]),/unexpected/);
for(const name of BUILD_ARTIFACTS) {
  const changed=manifest.map(row=>path.basename(row.path)===name?{...row,sha256:hash('other build')}:row);
  assert.throws(()=>validateFrozenBuildProfile(profile,changed),/disagrees with frozen build manifest/);
}

// Bad inventories must fail before even attempting HTTP. The separate driver
// suite exercises the same complete inventory against a real loopback server.
const originalFetch=globalThis.fetch;let requests=0;
try {
  globalThis.fetch=async()=>{++requests;throw Error('HTTP must not start');};
  for(const value of [undefined,null,{},[],{'custom.wasm':hash('custom')},
    {...artifacts,'unlisted.mjs':hash('extra')},{...artifacts,'runtime.html':'invalid'}])
    await assert.rejects(verifyServedArtifacts({artifacts:value}),/inventory|SHA-256/);
  assert.equal(requests,0);

  // Content hashes remain checked independently of inventory/manifest binding.
  globalThis.fetch=async url=>{
    ++requests;return new Response(path.basename(new URL(url).pathname));
  };
  const machine={...profile,url:'http://127.0.0.1/runtime.html'};
  const observed=await verifyServedArtifacts(machine);
  assert.equal(requests,14);
  for(const name of BUILD_ARTIFACTS)
    assert.deepEqual(observed[name],{sha256:hash(name),bytes:Buffer.byteLength(name)});
  globalThis.fetch=async()=>new Response('wrong executable');
  await assert.rejects(verifyServedArtifacts(machine),/Served artifact changed/);
} finally {globalThis.fetch=originalFetch;}
console.log('Hitch profile inventory checks passed');
