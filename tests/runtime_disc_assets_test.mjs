import assert from 'node:assert/strict';
import {loadRuntimeDisc,RUNTIME_DISC_FILES,loadNativeMenuDisc,NATIVE_MENU_DISC_FILES,loadNativeGameDisc,NATIVE_GAME_DISC_FILES} from '../web/runtime-assets.mjs';
assert.equal(Object.keys(RUNTIME_DISC_FILES).length,13);
for(const name of ['main.ssm','mario.ssm','smash2.sem'])assert.equal(RUNTIME_DISC_FILES[name],'audio/us/'+name);
await assert.rejects(loadRuntimeDisc({name:'game.rvz'}),/RVZ is not supported/);
const bytes=new Uint8Array(0x2000),view=new DataView(bytes.buffer);
bytes.set(new TextEncoder().encode('GALE01'));bytes[7]=2;
view.setUint32(0x1c,0xc2339f3d);view.setUint32(0x420,0x600);
await assert.rejects(loadRuntimeDisc(new Blob([bytes])),/unmodified USA revision/);
view.setUint32(0x600,0x100);view.setUint32(0x690,0x10000000);
await assert.rejects(loadRuntimeDisc(new Blob([bytes])),/Invalid game executable section/);
console.log('Runtime disc language paths and executable rejection checks passed');

assert.equal(Object.keys(NATIVE_MENU_DISC_FILES).length,15);
assert.equal(Object.keys(NATIVE_GAME_DISC_FILES).length,29);
for(const name of ['nr_select','nr_title','nr_name','pokemon','end']) {
  assert.equal(NATIVE_MENU_DISC_FILES[name+'.ssm'],'audio/us/'+name+'.ssm');
  assert.equal(NATIVE_GAME_DISC_FILES[name+'.ssm'],'audio/us/'+name+'.ssm');
}
for(const name of ['PlMrNr.dat','PlMrYe.dat','PlMrBk.dat','PlMrBu.dat','PlMrGr.dat'])
  assert.equal(NATIVE_GAME_DISC_FILES[name],name);
for(const loader of [loadNativeMenuDisc,loadNativeGameDisc]) {
  await assert.rejects(loader({name:'game.rvz'}),/RVZ is not supported/);
  await assert.rejects(loader(new Blob([bytes])),/Invalid game executable section/);
}
