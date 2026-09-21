import {openDiscImage,fontFileRange} from './disc-image.mjs';
import {openDiscSession} from './disc-session.mjs';

// Exact revision/language paths: audio/ also contains Japanese alternatives.
export const RUNTIME_DISC_FILES=Object.freeze({
  'PlCo.dat':'PlCo.dat','PlMr.dat':'PlMr.dat','PlMrNr.dat':'PlMrNr.dat',
  'PlMrAJ.dat':'PlMrAJ.dat','GrNLa.dat':'GrNLa.dat','ItCo.usd':'ItCo.usd',
  'EfMrData.dat':'EfMrData.dat','EfCoData.dat':'EfCoData.dat','PdPm.dat':'PdPm.dat',
  'LbRb.dat':'LbRb.dat',
  'smash2.sem':'audio/us/smash2.sem','main.ssm':'audio/us/main.ssm',
  'mario.ssm':'audio/us/mario.ssm','sp_end.hps':'audio/sp_end.hps',
});
export const ORIGINAL_DOL_SHA1='08e0bf20134dfcb260699671004527b2d6bb1a45';
const MAX_FILE=64*1024*1024,MAX_BUNDLE=128*1024*1024;
async function digest(kind,bytes){return Array.from(new Uint8Array(await crypto.subtle.digest(kind,bytes)),x=>x.toString(16).padStart(2,'0')).join('');}

export const NATIVE_MENU_DISC_FILES=Object.freeze({
  'MnSlChr.usd':'MnSlChr.usd','MnSlMap.usd':'MnSlMap.usd',
  'SdSlChr.usd':'SdSlChr.usd','MnExtAll.usd':'MnExtAll.usd',
  'LbMcGame.usd':'LbMcGame.usd','NtMemAc.usd':'NtMemAc.usd',
  'menu01.hps':'audio/menu01.hps','smash2.sem':'audio/us/smash2.sem',
  ...Object.fromEntries(['main','nr_select','nr_title','nr_name','pokemon','end',
    'captain','dk','fox','koopa','link','luigi','mario','mars','mewtwo','pikachu','purin',
    'falco','clink','drmario','emblem','pichu','ganon','pupupu']
    .map(name=>[name+'.ssm','audio/us/'+name+'.ssm']))
});
export const NATIVE_GAME_DISC_FILES=Object.freeze({
  ...NATIVE_MENU_DISC_FILES,...RUNTIME_DISC_FILES,
  'IfAll.usd':'IfAll.usd','IfCoGet.dat':'IfCoGet.dat','SdIntro.dat':'SdIntro.dat','GmPause.usd':'GmPause.usd',
  'LbBf.dat':'LbBf.dat',
  'PlMrYe.dat':'PlMrYe.dat','PlMrBk.dat':'PlMrBk.dat',
  'PlMrBu.dat':'PlMrBu.dat','PlMrGr.dat':'PlMrGr.dat',
  'PlFc.dat':'PlFc.dat','PlFcAJ.dat':'PlFcAJ.dat',
  'PlFcNr.dat':'PlFcNr.dat','PlFcRe.dat':'PlFcRe.dat',
  'PlFcBu.dat':'PlFcBu.dat','PlFcGr.dat':'PlFcGr.dat',
  'EfFxData.dat':'EfFxData.dat','falco.ssm':'audio/us/falco.ssm',
  'GrNBa.dat':'GrNBa.dat','sp_zako.hps':'audio/sp_zako.hps',
  'hyaku.hps':'audio/hyaku.hps','hyaku2.hps':'audio/hyaku2.hps',
  'PlFx.dat':'PlFx.dat','PlFxAJ.dat':'PlFxAJ.dat',
  'PlFxNr.dat':'PlFxNr.dat','PlFxOr.dat':'PlFxOr.dat',
  'PlFxLa.dat':'PlFxLa.dat','PlFxGr.dat':'PlFxGr.dat',
  'fox.ssm':'audio/us/fox.ssm',
  'GrSt.dat':'GrSt.dat','ystory.hps':'audio/ystory.hps',
  'PlMs.dat':'PlMs.dat','PlMsAJ.dat':'PlMsAJ.dat',
  'PlMsNr.dat':'PlMsNr.dat','PlMsRe.dat':'PlMsRe.dat','PlMsGr.dat':'PlMsGr.dat',
  'PlMsBk.dat':'PlMsBk.dat','PlMsWh.dat':'PlMsWh.dat',
  'EfMsData.dat':'EfMsData.dat','mars.ssm':'audio/us/mars.ssm',
  'GrOp.dat':'GrOp.dat','old_kb.hps':'audio/old_kb.hps','pupupu.ssm':'audio/us/pupupu.ssm',
  'GrSh.dat':'GrSh.dat','shrine.hps':'audio/shrine.hps','akaneia.hps':'audio/akaneia.hps',
  'GrIz.dat':'GrIz.dat','izumi.hps':'audio/izumi.hps',
  // Dr. Mario (source FighterKind 0x15) borrows Mario's effect bank but owns
  // its own fighter, action and costume archives and voice bank.
  'PlDr.dat':'PlDr.dat','PlDrAJ.dat':'PlDrAJ.dat',
  'PlDrNr.dat':'PlDrNr.dat','PlDrRe.dat':'PlDrRe.dat',
  'PlDrBu.dat':'PlDrBu.dat','PlDrGr.dat':'PlDrGr.dat','PlDrBk.dat':'PlDrBk.dat',
  'drmario.ssm':'audio/us/drmario.ssm',
  // Roy is source FighterKind 0x1a (CharacterKind 0x17), with a distinct
  // effect archive and the source emblem voice bank.
  'PlFe.dat':'PlFe.dat','PlFeAJ.dat':'PlFeAJ.dat',
  'PlFeNr.dat':'PlFeNr.dat','PlFeRe.dat':'PlFeRe.dat',
  'PlFeBu.dat':'PlFeBu.dat','PlFeGr.dat':'PlFeGr.dat','PlFeYe.dat':'PlFeYe.dat',
  'EfFeData.dat':'EfFeData.dat','emblem.ssm':'audio/us/emblem.ssm',
  // Link and Young Link share EfLkData.dat and the source effect table, but
  // retain their own fighter/costume archives and voice banks.
  'PlLk.dat':'PlLk.dat','PlLkAJ.dat':'PlLkAJ.dat',
  'PlLkNr.dat':'PlLkNr.dat','PlLkRe.dat':'PlLkRe.dat','PlLkBu.dat':'PlLkBu.dat',
  'PlLkBk.dat':'PlLkBk.dat','PlLkWh.dat':'PlLkWh.dat',
  'PlCl.dat':'PlCl.dat','PlClAJ.dat':'PlClAJ.dat',
  'PlClNr.dat':'PlClNr.dat','PlClRe.dat':'PlClRe.dat','PlClBu.dat':'PlClBu.dat',
  'PlClWh.dat':'PlClWh.dat','PlClBk.dat':'PlClBk.dat',
  'EfLkData.dat':'EfLkData.dat','link.ssm':'audio/us/link.ssm','clink.ssm':'audio/us/clink.ssm',
  'PlCa.dat':'PlCa.dat','PlCaAJ.dat':'PlCaAJ.dat','PlCaNr.dat':'PlCaNr.dat',
  'PlCaGy.dat':'PlCaGy.dat','PlCaRe.usd':'PlCaRe.usd','PlCaWh.dat':'PlCaWh.dat',
  'PlCaGr.dat':'PlCaGr.dat','PlCaBu.dat':'PlCaBu.dat',
  'EfCaData.dat':'EfCaData.dat','captain.ssm':'audio/us/captain.ssm',
  'PlGn.dat':'PlGn.dat','PlGnAJ.dat':'PlGnAJ.dat',
  'PlGnNr.dat':'PlGnNr.dat','PlGnRe.dat':'PlGnRe.dat','PlGnBu.dat':'PlGnBu.dat',
  'PlGnGr.dat':'PlGnGr.dat','PlGnLa.dat':'PlGnLa.dat',
  'EfGnData.dat':'EfGnData.dat','ganon.ssm':'audio/us/ganon.ssm',
  'PlLg.dat':'PlLg.dat','PlLgAJ.dat':'PlLgAJ.dat','PlLgNr.dat':'PlLgNr.dat',
  'PlLgWh.dat':'PlLgWh.dat','PlLgAq.dat':'PlLgAq.dat','PlLgPi.dat':'PlLgPi.dat',
  'EfLgData.dat':'EfLgData.dat','luigi.ssm':'audio/us/luigi.ssm',
  'PlPk.dat':'PlPk.dat','PlPkAJ.dat':'PlPkAJ.dat','PlPkNr.dat':'PlPkNr.dat',
  'PlPkRe.dat':'PlPkRe.dat','PlPkBu.dat':'PlPkBu.dat','PlPkGr.dat':'PlPkGr.dat',
  'PlPc.dat':'PlPc.dat','PlPcAJ.dat':'PlPcAJ.dat','PlPcNr.dat':'PlPcNr.dat',
  'PlPcRe.dat':'PlPcRe.dat','PlPcBu.dat':'PlPcBu.dat','PlPcGr.dat':'PlPcGr.dat',
  'EfPkData.dat':'EfPkData.dat',
  'pikachu.ssm':'audio/us/pikachu.ssm','pichu.ssm':'audio/us/pichu.ssm',
  'GrOy.dat':'GrOy.dat','old_ys.hps':'audio/old_ys.hps',
  'PlPr.dat':'PlPr.dat','PlPrAJ.dat':'PlPrAJ.dat','PlPrNr.dat':'PlPrNr.dat',
  'PlPrRe.dat':'PlPrRe.dat','PlPrBu.dat':'PlPrBu.dat','PlPrGr.dat':'PlPrGr.dat','PlPrYe.dat':'PlPrYe.dat',
  'EfPrData.dat':'EfPrData.dat','purin.ssm':'audio/us/purin.ssm',
  'PlDk.dat':'PlDk.dat','PlDkAJ.dat':'PlDkAJ.dat','PlDkNr.dat':'PlDkNr.dat',
  'PlDkBk.dat':'PlDkBk.dat','PlDkRe.dat':'PlDkRe.dat','PlDkBu.dat':'PlDkBu.dat','PlDkGr.dat':'PlDkGr.dat',
  'EfDkData.dat':'EfDkData.dat','dk.ssm':'audio/us/dk.ssm',
  'PlKp.dat':'PlKp.dat','PlKpAJ.dat':'PlKpAJ.dat','PlKpNr.dat':'PlKpNr.dat',
  'PlKpRe.dat':'PlKpRe.dat','PlKpBu.dat':'PlKpBu.dat','PlKpBk.dat':'PlKpBk.dat',
  'EfKpData.dat':'EfKpData.dat','koopa.ssm':'audio/us/koopa.ssm',
  'PlMt.dat':'PlMt.dat','PlMtAJ.dat':'PlMtAJ.dat','PlMtNr.dat':'PlMtNr.dat',
  'PlMtRe.dat':'PlMtRe.dat','PlMtBu.dat':'PlMtBu.dat','PlMtGr.dat':'PlMtGr.dat',
  'EfMtData.dat':'EfMtData.dat','mewtwo.ssm':'audio/us/mewtwo.ssm',
});
export function loadNativeGameDisc(file,report=()=>{}) {
  return loadDiscBundle(file,report,NATIVE_GAME_DISC_FILES);
}
/**
 * Open a silent public native-disc session. The native scope supplies the
 * exact logical names for each scene; the shared session preflights every
 * corresponding FST path before reading any payload.
 */
export async function openNativeGameDiscSession(file) {
  const session = await openDiscSession(file);
  return Object.freeze({
    close: () => session.close(),
    async readScope(names, report = () => {}) {
      const paths = Object.create(null), seen = new Set();
      for (const name of names) {
        if (typeof name !== 'string' || seen.has(name))
          throw Error('Invalid or duplicate native asset name.');
        seen.add(name);
        if (name === 'sislib_font.bin') continue;
        if (name === 'dsp_coef.bin')
          throw Error('Public native scenes do not accept DSP coefficients.');
        if (!Object.hasOwn(NATIVE_GAME_DISC_FILES, name))
          throw Error('Unknown native scene asset: ' + name);
        paths[name] = NATIVE_GAME_DISC_FILES[name];
      }
      const total = names.length;
      report({phase: 'validate', complete: 0, total});
      const files = await session.readScope(paths, {
        beforeRead: ({name, index}) =>
          report({phase: 'read', file: name, complete: index, total}),
      });
      if (seen.has('sislib_font.bin')) files.set('sislib_font.bin', session.fontBytes());
      report({phase: 'complete', complete: total, total});
      session.metadata();
      return files;
    },
  });
}
/** Read only the selected source scene's data; no upload or persistence. */
export function loadRuntimeDisc(file,report=()=>{}) {
  return loadDiscBundle(file,report,RUNTIME_DISC_FILES);
}
export function loadNativeMenuDisc(file,report=()=>{}) {
  return loadDiscBundle(file,report,NATIVE_MENU_DISC_FILES);
}
async function loadDiscBundle(file,report,paths) {
  const total=Object.keys(paths).length+1;
  if(/\.rvz$/i.test(file.name??''))throw Error('RVZ is not supported yet. Choose an ISO, GCM, or CISO image.');
  report({phase:'validate',complete:0,total});
  const disc=await openDiscImage(file);
  const pointer=await disc.read(0x420,4),dolOffset=new DataView(pointer.buffer,pointer.byteOffset,4).getUint32(0);
  if(dolOffset<0x440)throw Error('Invalid game executable location.');
  const header=await disc.read(dolOffset,0x100),view=new DataView(header.buffer,header.byteOffset,header.byteLength);
  let dolSize=0x100;
  for(let i=0;i<18;i++){
    const offset=view.getUint32(i*4),size=view.getUint32(0x90+i*4);
    if(size){if(offset<0x100||offset+size>MAX_FILE)throw Error('Invalid game executable section.');dolSize=Math.max(dolSize,offset+size);}
  }
  const dol=await disc.read(dolOffset,dolSize);
  if(await digest('SHA-1',dol)!==ORIGINAL_DOL_SHA1)throw Error('This build requires the unmodified USA revision 1.02 executable. This disc does not match.');
  const entries=await disc.files();let totalBytes=0;
  for(const path of Object.values(paths)){
    const entry=entries.get(path);
    if(!entry)throw Error('Required game data is missing: '+path);
    if(!entry.size||entry.size>MAX_FILE)throw Error('Invalid game file size: '+path);
    totalBytes+=entry.size;
  }
  if(totalBytes>MAX_BUNDLE)throw Error('Required game data exceeds the current import budget.');
  const result=new Map();
  for(const [name,path] of Object.entries(paths)){
    report({phase:'read',file:name,complete:result.size,total});
    result.set(name,await disc.readFile(path));
  }
  const font=fontFileRange(header);
  if(font.offset+font.size>dol.byteLength)throw Error('Font data is outside the validated executable.');
  result.set('sislib_font.bin',dol.slice(font.offset,font.offset+font.size));
  report({phase:'complete',complete:total,total});
  return result;
}
