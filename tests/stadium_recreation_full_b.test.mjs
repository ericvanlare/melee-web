import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {test} from 'node:test';
import {hostEvidenceFromPacket,encodeRun2FullCommonNativeManifest} from './stadium_matched_setup_cf_consumer.mjs';
const source=await fs.readFile(new URL('./stadium_c1a_browser_test.mjs',import.meta.url),'utf8');
const begin=source.indexOf("if (recreationFullPair && values['recreation-canary-full-b'])");
const end=source.indexOf('if ((firstSssVsSetup &&',begin);
assert(begin>=0&&end>begin);
const guards=source.slice(begin,end);
const valid={recreationFullPair:false,recreationFullB:false,recreationCanary:true,firstSssVsSetup:true,
  values:{'first-sss-vs-setup-cf':true},firstSssVsSetupCfExplicitStep:true,
  firstSssVsSetupCfFullSchedule:false,firstSssVsSetupCfCommonFields:false,firstSssVsSetupTextTail:false};
test('actual CLI guard keeps A prefix and requires separate full-B canary opt-in',()=>{
  for(const recreationFullB of [false,true])vm.runInNewContext(guards,{...valid,recreationFullB});
  vm.runInNewContext(guards,{...valid,recreationFullPair:true,recreationFullB:true,firstSssVsSetupCfFullSchedule:true,firstSssVsSetupCfCommonFields:true});
  for(const changes of [{recreationFullPair:true,recreationFullB:true,values:{...valid.values,'recreation-canary-full-b':true}},
    {recreationFullPair:true,recreationFullB:true,values:{...valid.values,'first-sss-vs-setup-cf-full-schedule':true}}, {recreationFullB:true,recreationCanary:false},
    {recreationFullB:true,firstSssVsSetupCfFullSchedule:true},
    {recreationFullB:true,firstSssVsSetupCfCommonFields:true},
    {recreationFullB:true,firstSssVsSetupCfExplicitStep:false},
    {recreationFullB:true,firstSssVsSetupCfTextTail:true,firstSssVsSetupTextTail:true}])
    assert.throws(()=>vm.runInNewContext(guards,{...valid,...changes}),/requires/);
});
test('actual B source switches only after A restart and uses existing full loader/adapter',()=>{
  const accepted=source.indexOf('globalThis[helperKey].acceptRestart(receipt)');
  const switched=source.indexOf('matchedSetupCfInputs = matchedSetupCfBInputs;');
  const imported=source.indexOf('Fresh B disc import after checked same-instance restart');
  assert(accepted>=0&&switched>accepted&&imported>switched);
  assert(source.includes('fullSchedule: !firstSssFresh132Typed22Pair, commonFields: !firstSssFresh132Typed22Pair, frozen: matchedSetupCfBPreflight'));
  const loaderExpression=source.match(/loadMatchedSetupCfInputs\(\{\n    (fullSchedule: !firstSssFresh132Typed22Pair, commonFields: !firstSssFresh132Typed22Pair, frozen: matchedSetupCfBPreflight)\}\)/)[1];
  for(const typed22 of [false,true]) {const admitted=vm.runInNewContext('({'+loaderExpression+'})',{firstSssFresh132Typed22Pair:typed22,matchedSetupCfBPreflight:{identity:'same-input'}});assert.equal(admitted.fullSchedule,!typed22);assert.equal(admitted.commonFields,!typed22);assert.equal(admitted.frozen.identity,'same-input');}
  assert(source.includes('preflight.recreation_canary?.full_b_matched_setup_cf'));
  const valueExpression=source.match(/cf.exact_value_comparisons_passed, (firstSssFresh132Typed22Pair\?80523:56016)/)[1];
  assert.equal(vm.runInNewContext(valueExpression,{firstSssFresh132Typed22Pair:false}),56016);assert.equal(vm.runInNewContext(valueExpression,{firstSssFresh132Typed22Pair:true}),80523);
  assert(source.includes('bRng.full_common_fields_seed, true'));
});
const packetFile=process.env.MELEE_STADIUM_FULL_B_PACKET;
test('retained original packet derives independent A174 and B7167/version5/3501 rows',
  {skip:!packetFile},async()=>{
    const bytes=await fs.readFile(packetFile);
    assert.equal(createHash('sha256').update(bytes).digest('hex'),process.env.MELEE_STADIUM_FULL_B_PACKET_SHA256);
    const packet=JSON.parse(bytes);const binding=packet.host_only.source_identity.source_run.run_artifacts['idle3500-reference.json'];
    const seedBytes=await fs.readFile(binding.path);
    assert.equal(createHash('sha256').update(seedBytes).digest('hex'),binding.sha256);
    const seedIdentity={path:binding.path,basename:path.basename(binding.path),bytes:seedBytes.length,sha256:binding.sha256};
    const A=hostEvidenceFromPacket(packet);const B=hostEvidenceFromPacket(packet,{fullSchedule:true,commonFields:true,seedSidecar:JSON.parse(seedBytes),seedIdentity});
    assert.equal(A.mode,'run2-proper-prefix-v2');assert.equal(A.runtimePayload.schedule.length,174);
    assert.equal(B.mode,'run2-full-common-fields-seed-v1');assert.equal(B.runtimePayload.schedule.length,7167);
    assert.equal(B.runtimePayload.bundles.length,3585);assert.equal(B.commonRowsByAction.size,3501);
    assert.equal(B.runtimePayload.schedule.filter(x=>x.action==='draw').length,3582);
    assert.equal(Buffer.from(encodeRun2FullCommonNativeManifest(B.runtimePayload).subarray(0,4)).toString(),'MWC5');
    assert.throws(()=>hostEvidenceFromPacket(packet,{fullSchedule:false,commonFields:true}),/Per-tick common fields require the explicit full schedule/);
    const wrong=structuredClone(packet);wrong.runtime_payload.schedule.pop();
    assert.throws(()=>hostEvidenceFromPacket(wrong,{fullSchedule:true,commonFields:true,seedSidecar:JSON.parse(seedBytes),seedIdentity}));
  });
