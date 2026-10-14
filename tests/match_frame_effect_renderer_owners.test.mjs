import test from 'node:test';
import assert from 'node:assert/strict';
import {rendererOwnerObservationFailures} from './match_frame_effect_renderer_owners.mjs';
import {schema2Fixture} from './match_frame_effect_schema2_fixture.mjs';
import {frameEffectObservationFailures,compareFrameEffectObservations} from './match_frame_effect_driver.mjs';
import {firstDrawObservationFailures,compareFirstDrawObservations} from './match_frame_effect_first_draw_driver.mjs';
function currentFixture(firstDraw=false) {
  const r=schema2Fixture();r.schema_version=3;r.renderer.schema_version=3;
  r.layout_entries=[{layout_id:'5',entry_index:0,words:[0,3,0,0,1,1,0,0,0,0,0,0,0,0,0]}];
  r.provenance=[
    {capture_id:'3',device_id:'1',creation_serial:'9007199254740993',renderer_epoch:'18446744073709551615',role:3,slot:0},
    {capture_id:'5',device_id:'1',creation_serial:'9007199254740994',renderer_epoch:'18446744073709551615',role:7,slot:0},
    {capture_id:'4',device_id:'1',creation_serial:'9007199254740995',renderer_epoch:'18446744073709551615',role:10,slot:0}];
  r.handles.push({capture_id:'5',kind:9,reserved:0});r.renderer.handle_count++;
  r.resources.push({capture_id:'5',owner_id:'1',kind:9,word_count:1,words:[1,...Array(15).fill(0)]});r.renderer.resource_count++;
  r.resources[2].words[0]=5;
  r.binding_entries[0].binding=0;r.binding_entries[0].offset='0';r.binding_entries[0].size='3840';
  r.renderer.layout_entry_count=1;r.renderer.provenance_count=3;
  if(firstDraw){r.schema_version=4;r.scope='first_draw_in_normal_callback_observation_only';
    r.selected_source_frame=201;r.unobserved_draws=0;
    r.selected_depth_owners={registered:0,completed:0,pending:0,ignored_other_issuer:0,finalized:false};}
  return r;
}
test('current layout/provenance schema dispatch preserves strict historical versions',()=>{
  const old=schema2Fixture();assert.deepEqual(frameEffectObservationFailures(old),[]);
  const current=currentFixture();assert.deepEqual(frameEffectObservationFailures(current),[]);
  assert.deepEqual(compareFrameEffectObservations(current,structuredClone(current)),[]);
  const first=currentFixture(true);assert.deepEqual(firstDrawObservationFailures(first),[]);
  assert.deepEqual(compareFirstDrawObservations(first,structuredClone(first)),[]);
  for(const mutate of [r=>{r.schema_version=2;},r=>{r.renderer.schema_version=2;},
    r=>{delete r.layout_entries;},r=>{delete r.provenance;},r=>{r.extra=0;},
    r=>{r.renderer.layout_entry_count++;},r=>{r.provenance[1].creation_serial=r.provenance[0].creation_serial;},
    r=>{r.provenance[0].renderer_epoch='1';},r=>{r.provenance[1].device_id='2';},
    r=>{r.provenance[1].role=6;},r=>{r.layout_entries[0].entry_index=1;},
    r=>{r.layout_entries[0].words.pop();},r=>{r.layout_entries.push(structuredClone(r.layout_entries[0]));r.renderer.layout_entry_count++;},
    r=>{r.resources.at(-1).words[0]=2;},r=>{r.provenance[0].slot=1;},
    r=>{r.binding_entries[0].resource_id='2';},r=>{r.resources[2].words[0]=3;}]) {
    const bad=structuredClone(current);mutate(bad);assert.ok(frameEffectObservationFailures(bad).length);
  }
});
test('all layout high words and uint64 creation identities compare exactly without coercion',()=>{
  const r=currentFixture();
  for(const mutate of [c=>{c.layout_entries[0].words[2]=1;},c=>{c.layout_entries[0].words[7]=1;},
    c=>{c.provenance[0].creation_serial='9007199254740996';}]) {
    const c=structuredClone(r);mutate(c);assert.ok(compareFrameEffectObservations(r,c).length);
  }
});

test('malformed provenance rows reject without throwing or entering the lookup',()=>{
  const base=currentFixture();
  for(const badRow of [null,undefined,17,'owner',[],{},
    {...base.provenance[0],creation_serial:null},
    {...base.provenance[0],capture_id:null},
    {...base.provenance[0],extra:true}]) {
    const bad=structuredClone(base);bad.provenance[0]=badRow;
    let failures;
    assert.doesNotThrow(()=>{failures=rendererOwnerObservationFailures(bad);});
    assert.ok(failures.includes('owner_provenance_rows'));
    assert.doesNotThrow(()=>{failures=frameEffectObservationFailures(bad);});
    assert.ok(failures.length);
  }
  for(const badRows of [null,{},17]) {
    const bad=structuredClone(base);bad.provenance=badRows;
    assert.doesNotThrow(()=>assert.ok(rendererOwnerObservationFailures(bad).length));
  }
});

test('null and primitive rows in every owner-helper collection refuse in both public schemas',()=>{
  for(const firstDraw of [false,true])for(const key of ['resources','handles','binding_entries','layout_entries','provenance']) {
    for(const malformed of [null,undefined,17,'row',[],{}]) {
      const r=currentFixture(firstDraw);r[key].push(malformed);
      if(key==='resources')r.renderer.resource_count++;
      if(key==='handles')r.renderer.handle_count++;
      if(key==='binding_entries')r.renderer.binding_entry_count++;
      if(key==='layout_entries')r.renderer.layout_entry_count++;
      if(key==='provenance')r.renderer.provenance_count++;
      let failures;
      assert.doesNotThrow(()=>{failures=rendererOwnerObservationFailures(r);});assert.ok(failures.length);
      const validate=firstDraw?firstDrawObservationFailures:frameEffectObservationFailures;
      assert.doesNotThrow(()=>{failures=validate(r);});assert.ok(failures.length);
    }
  }
});

test('invalid exact-shaped raw rows cannot enter owner graph lookups',()=>{
  for(const firstDraw of [false,true])for(const mutate of [
    r=>{r.resources.at(-1).words=null;},r=>{r.resources[0].word_count='2';},
    r=>{r.handles[0].capture_id=null;},r=>{r.binding_entries[0].offset=null;},
    r=>{r.provenance[1].device_id=null;}]) {
    const r=currentFixture(firstDraw);mutate(r);let failures;
    assert.doesNotThrow(()=>{failures=rendererOwnerObservationFailures(r);});assert.ok(failures.length);
    const validate=firstDraw?firstDrawObservationFailures:frameEffectObservationFailures;
    assert.doesNotThrow(()=>{failures=validate(r);});assert.ok(failures.length);
  }
});

function attachmentFixture(firstDraw=false,slot=0) {
  const r=currentFixture(firstDraw);r.schema_version=firstDraw?5:4;r.renderer.schema_version=4;
  r.handles.push({capture_id:'6',kind:2,reserved:0},{capture_id:'7',kind:1,reserved:0});r.renderer.handle_count+=2;
  r.resources.push({capture_id:'6',owner_id:'1',kind:2,word_count:10,words:[640,480,1,1,2,1,1,16,0,0,...Array(6).fill(0)]},
    {capture_id:'7',owner_id:'6',kind:1,word_count:10,words:[6,0,0,2,0,1,0,1,0,0,...Array(6).fill(0)]});r.renderer.resource_count+=2;
  r.provenance.push({capture_id:'6',device_id:'1',creation_serial:'9007199254740996',renderer_epoch:'18446744073709551615',role:15,slot},
    {capture_id:'7',device_id:'1',creation_serial:'9007199254740997',renderer_epoch:'18446744073709551615',role:16,slot});r.renderer.provenance_count+=2;
  return r;
}
test('renderer4/normal4/first5 exact attachment slots while historical renderer3 refuses new roles',()=>{
  for(const firstDraw of [false,true])for(const slot of [0,1,2]){
    const r=attachmentFixture(firstDraw,slot);const validate=firstDraw?firstDrawObservationFailures:frameEffectObservationFailures;
    const compare=firstDraw?compareFirstDrawObservations:compareFrameEffectObservations;
    assert.deepEqual(validate(r),[]);assert.deepEqual(compare(r,structuredClone(r)),[]);
    for(const mutate of [x=>{x.renderer.schema_version=3;},x=>{x.schema_version=firstDraw?4:3;x.renderer.schema_version=3;},
      x=>{x.provenance.at(-1).slot=3;},x=>{x.provenance.at(-2).slot=3;},x=>{x.provenance.at(-1).role=17;},
      x=>{x.provenance.at(-1).slot=(slot+1)%3;},x=>{x.provenance.at(-2).role=12;},x=>{x.resources.at(-1).owner_id='3';}]){
      const bad=structuredClone(r);mutate(bad);assert.ok(validate(bad).length);
    }
    const different=structuredClone(r);different.provenance.at(-1).creation_serial='9007199254740998';assert.ok(compare(r,different).length);
  }
  for(const [schema,bound] of [[3,17],[4,23]]){
    const r=attachmentFixture();r.renderer.schema_version=schema;r.provenance=Array(bound+1).fill(null);r.renderer.provenance_count=bound+1;
    assert.ok(rendererOwnerObservationFailures(r).includes('provenance_count'));
  }
});
