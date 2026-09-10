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
assert.equal(Object.keys(NATIVE_GAME_DISC_FILES).length,64);
for(const name of ['nr_select','nr_title','nr_name','pokemon','end']) {
  assert.equal(NATIVE_MENU_DISC_FILES[name+'.ssm'],'audio/us/'+name+'.ssm');
  assert.equal(NATIVE_GAME_DISC_FILES[name+'.ssm'],'audio/us/'+name+'.ssm');
}
for(const name of ['GmPause.usd','IfAll.usd','IfCoGet.dat','SdIntro.dat',
  'PlMrNr.dat','PlMrYe.dat','PlMrBk.dat','PlMrBu.dat','PlMrGr.dat',
  'PlFc.dat','PlFcAJ.dat','PlFcNr.dat','PlFcRe.dat','PlFcBu.dat','PlFcGr.dat',
  'PlFx.dat','PlFxAJ.dat','PlFxNr.dat','PlFxOr.dat','PlFxLa.dat','PlFxGr.dat',
  'EfFxData.dat','GrNBa.dat','GrSt.dat','PlMs.dat','PlMsAJ.dat','PlMsNr.dat',
  'PlMsRe.dat','PlMsGr.dat','PlMsBk.dat','PlMsWh.dat','EfMsData.dat','GrOp.dat'])
  assert.equal(NATIVE_GAME_DISC_FILES[name],name);
assert.equal(NATIVE_GAME_DISC_FILES['falco.ssm'],'audio/us/falco.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['fox.ssm'],'audio/us/fox.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['mars.ssm'],'audio/us/mars.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['pupupu.ssm'],'audio/us/pupupu.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['sp_zako.hps'],'audio/sp_zako.hps');
assert.equal(NATIVE_GAME_DISC_FILES['ystory.hps'],'audio/ystory.hps');
assert.equal(NATIVE_GAME_DISC_FILES['greens.hps'],'audio/greens.hps');
for(const loader of [loadNativeMenuDisc,loadNativeGameDisc]) {
  await assert.rejects(loader({name:'game.rvz'}),/RVZ is not supported/);
  await assert.rejects(loader(new Blob([bytes])),/Invalid game executable section/);
}
