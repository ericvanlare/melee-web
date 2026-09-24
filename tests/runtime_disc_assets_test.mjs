import assert from 'node:assert/strict';
import {loadRuntimeDisc,RUNTIME_DISC_FILES,loadNativeMenuDisc,NATIVE_MENU_DISC_FILES,loadNativeGameDisc,NATIVE_GAME_DISC_FILES} from '../web/runtime-audio-assets.mjs';
assert.equal(Object.keys(RUNTIME_DISC_FILES).length,14);
assert.equal(RUNTIME_DISC_FILES['LbRb.dat'],'LbRb.dat');
for(const name of ['main.ssm','mario.ssm','smash2.sem'])assert.equal(RUNTIME_DISC_FILES[name],'audio/us/'+name);
await assert.rejects(loadRuntimeDisc({name:'game.rvz'}),/RVZ is not supported/);
const bytes=new Uint8Array(0x2000),view=new DataView(bytes.buffer);
bytes.set(new TextEncoder().encode('GALE01'));bytes[7]=2;
view.setUint32(0x1c,0xc2339f3d);view.setUint32(0x420,0x600);
await assert.rejects(loadRuntimeDisc(new Blob([bytes])),/unmodified USA revision/);
view.setUint32(0x600,0x100);view.setUint32(0x690,0x10000000);
await assert.rejects(loadRuntimeDisc(new Blob([bytes])),/Invalid game executable section/);
console.log('Runtime disc language paths and executable rejection checks passed');

assert.equal(Object.keys(NATIVE_MENU_DISC_FILES).length,33);
assert.equal(NATIVE_MENU_DISC_FILES['LbRb.dat'],'LbRb.dat');
for (const name of Object.keys(NATIVE_MENU_DISC_FILES).filter(name => name.endsWith('.ssm')))
  assert.equal(NATIVE_MENU_DISC_FILES[name], 'audio/us/' + name);
assert.equal(Object.keys(NATIVE_GAME_DISC_FILES).length,217);
// The original Results and Prize scenes run over their own authored archives
// and voice streams; the scoped import must find each at its authored path.
for(const name of ['GmRst.usd','SdRst.usd','TyDatai.usd','IfPrize.usd','SdPrize.usd',
  'GmRstMMr.dat','GmRstMDr.dat','GmRstMFx.dat','GmRstMFc.dat',
  'GmRstMMs.dat','GmRstMFe.dat','GmRstMLk.dat','GmRstMCl.dat',
  'GmRstMCa.dat','GmRstMDk.dat','GmRstMGn.dat','GmRstMKp.dat',
  'GmRstMLg.dat','GmRstMMt.dat','GmRstMPk.dat','GmRstMPc.dat','GmRstMPr.dat'])
  assert.equal(NATIVE_GAME_DISC_FILES[name],name);
for(const [name,path] of Object.entries({
  's_info1.hps':'audio/s_info1.hps','s_info2.hps':'audio/s_info2.hps','s_info3.hps':'audio/s_info3.hps',
  'ff_mario.hps':'audio/ff_mario.hps','ff_fox.hps':'audio/ff_fox.hps',
  'ff_emb.hps':'audio/ff_emb.hps','ff_link.hps':'audio/ff_link.hps',
  'ff_fzero.hps':'audio/ff_fzero.hps','ff_dk.hps':'audio/ff_dk.hps',
  'ff_poke.hps':'audio/ff_poke.hps',
})) assert.equal(NATIVE_GAME_DISC_FILES[name],path);
for(const name of ['nr_select','nr_title','nr_name','pokemon','end']) {
  assert.equal(NATIVE_MENU_DISC_FILES[name+'.ssm'],'audio/us/'+name+'.ssm');
  assert.equal(NATIVE_GAME_DISC_FILES[name+'.ssm'],'audio/us/'+name+'.ssm');
}
for(const name of ['LbRb.dat','LbBf.dat','GmPause.usd','IfAll.usd','IfCoGet.dat','SdIntro.dat',
  'PlMrNr.dat','PlMrYe.dat','PlMrBk.dat','PlMrBu.dat','PlMrGr.dat',
  'PlFc.dat','PlFcAJ.dat','PlFcNr.dat','PlFcRe.dat','PlFcBu.dat','PlFcGr.dat',
  'PlFx.dat','PlFxAJ.dat','PlFxNr.dat','PlFxOr.dat','PlFxLa.dat','PlFxGr.dat',
  'EfFxData.dat','GrNBa.dat','GrSt.dat','PlMs.dat','PlMsAJ.dat','PlMsNr.dat',
  'PlMsRe.dat','PlMsGr.dat','PlMsBk.dat','PlMsWh.dat','EfMsData.dat','GrOp.dat','GrSh.dat',
  'PlDr.dat','PlDrAJ.dat','PlDrNr.dat','PlDrRe.dat','PlDrBu.dat','PlDrGr.dat','PlDrBk.dat',
  'PlFe.dat','PlFeAJ.dat','PlFeNr.dat','PlFeRe.dat','PlFeBu.dat','PlFeGr.dat','PlFeYe.dat',
  'EfFeData.dat',
  'PlLk.dat','PlLkAJ.dat','PlLkNr.dat','PlLkRe.dat','PlLkBu.dat','PlLkBk.dat','PlLkWh.dat',
  'PlCl.dat','PlClAJ.dat','PlClNr.dat','PlClRe.dat','PlClBu.dat','PlClWh.dat','PlClBk.dat',
  'EfLkData.dat','PlGn.dat','PlGnAJ.dat','PlGnNr.dat','PlGnRe.dat','PlGnBu.dat',
  'PlGnGr.dat','PlGnLa.dat','EfGnData.dat',
  'PlCa.dat','PlCaAJ.dat','PlCaNr.dat','PlCaGy.dat','PlCaRe.usd','PlCaWh.dat',
  'PlCaGr.dat','PlCaBu.dat','EfCaData.dat','GrIz.dat',
  'PlLg.dat','PlLgAJ.dat','PlLgNr.dat','PlLgWh.dat','PlLgAq.dat','PlLgPi.dat','EfLgData.dat',
  'PlPk.dat','PlPkAJ.dat','PlPkNr.dat','PlPkRe.dat','PlPkBu.dat','PlPkGr.dat',
  'PlPc.dat','PlPcAJ.dat','PlPcNr.dat','PlPcRe.dat','PlPcBu.dat','PlPcGr.dat',
  'EfPkData.dat','GrOy.dat',
  'PlPr.dat','PlPrAJ.dat','PlPrNr.dat','PlPrRe.dat','PlPrBu.dat','PlPrGr.dat','PlPrYe.dat','EfPrData.dat',
  'PlDk.dat','PlDkAJ.dat','PlDkNr.dat','PlDkBk.dat','PlDkRe.dat','PlDkBu.dat','PlDkGr.dat','EfDkData.dat'])
  assert.equal(NATIVE_GAME_DISC_FILES[name],name);
assert.equal(NATIVE_GAME_DISC_FILES['falco.ssm'],'audio/us/falco.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['fox.ssm'],'audio/us/fox.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['mars.ssm'],'audio/us/mars.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['drmario.ssm'],'audio/us/drmario.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['emblem.ssm'],'audio/us/emblem.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['captain.ssm'],'audio/us/captain.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['ganon.ssm'],'audio/us/ganon.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['luigi.ssm'],'audio/us/luigi.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['pikachu.ssm'],'audio/us/pikachu.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['pichu.ssm'],'audio/us/pichu.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['purin.ssm'],'audio/us/purin.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['dk.ssm'],'audio/us/dk.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['mewtwo.ssm'],'audio/us/mewtwo.ssm');
for(const name of ['PlMt.dat','PlMtAJ.dat','PlMtNr.dat','PlMtRe.dat','PlMtBu.dat','PlMtGr.dat','EfMtData.dat'])
  assert.equal(NATIVE_GAME_DISC_FILES[name],name);
assert.equal(NATIVE_GAME_DISC_FILES['old_ys.hps'],'audio/old_ys.hps');
assert.equal(NATIVE_GAME_DISC_FILES['pupupu.ssm'],'audio/us/pupupu.ssm');
assert.equal(NATIVE_GAME_DISC_FILES['sp_zako.hps'],'audio/sp_zako.hps');
assert.equal(NATIVE_GAME_DISC_FILES['ystory.hps'],'audio/ystory.hps');
assert.equal(NATIVE_GAME_DISC_FILES['old_kb.hps'],'audio/old_kb.hps');
assert.equal(NATIVE_GAME_DISC_FILES['shrine.hps'],'audio/shrine.hps');
assert.equal(NATIVE_GAME_DISC_FILES['akaneia.hps'],'audio/akaneia.hps');
assert.equal(NATIVE_GAME_DISC_FILES['izumi.hps'],'audio/izumi.hps');
for(const loader of [loadNativeMenuDisc,loadNativeGameDisc]) {
  await assert.rejects(loader({name:'game.rvz'}),/RVZ is not supported/);
  await assert.rejects(loader(new Blob([bytes])),/Invalid game executable section/);
}

for(const name of ['hyaku.hps','hyaku2.hps'])assert.equal(NATIVE_GAME_DISC_FILES[name],'audio/'+name);
