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
  'LbRb.dat':'LbRb.dat',
  'menu01.hps':'audio/menu01.hps','smash2.sem':'audio/us/smash2.sem',
  ...Object.fromEntries(['main','nr_select','nr_title','nr_name','pokemon','end',
    'captain','dk','fox','koopa','link','luigi','mario','mars','ness','peach','pikachu','purin',
    'mewtwo','falco','clink','drmario','emblem','pichu','ganon','pupupu','kirby','samus','yoshi','zs','gw','ice']
    .map(name=>[name+'.ssm','audio/us/'+name+'.ssm']))
});
export const NATIVE_GAME_DISC_FILES=Object.freeze({
  ...NATIVE_MENU_DISC_FILES,...RUNTIME_DISC_FILES,
  'IfAll.usd':'IfAll.usd','IfCoGet.dat':'IfCoGet.dat','SdIntro.dat':'SdIntro.dat','GmPause.usd':'GmPause.usd',
  'LbBf.dat':'LbBf.dat',
  'GmRst.usd':'GmRst.usd','SdRst.usd':'SdRst.usd','TyDatai.usd':'TyDatai.usd',
  's_info1.hps':'audio/s_info1.hps','s_info2.hps':'audio/s_info2.hps','s_info3.hps':'audio/s_info3.hps','IfPrize.usd':'IfPrize.usd','SdPrize.usd':'SdPrize.usd',
  ...Object.fromEntries(['Mr','Dr','Fx','Fc','Ms','Fe','Lk','Cl','Ca','Dk','Gn','Kp','Lg','Mt','Ns','Pe','Pk','Pc','Pr','Gw','Kb','Ss','Zd','Sk']
    .map(kind=>[`GmRstM${kind}.dat`,`GmRstM${kind}.dat`])),
  ...Object.fromEntries(['mario','fox','emb','link','fzero','dk','poke','nes']
    .map(name=>[`ff_${name}.hps`,`audio/ff_${name}.hps`])),
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
  // Ness is source FighterKind 8 (CharacterKind 0x0B) with four costume
  // owners and its own effect archive and voice bank.
  'PlNs.dat':'PlNs.dat','PlNsAJ.dat':'PlNsAJ.dat','PlNsNr.dat':'PlNsNr.dat',
  'PlNsYe.dat':'PlNsYe.dat','PlNsBu.dat':'PlNsBu.dat','PlNsGr.dat':'PlNsGr.dat',
  'EfNsData.dat':'EfNsData.dat','ness.ssm':'audio/us/ness.ssm',
  // Peach is source FighterKind 9 (CharacterKind 0x0C) with five costume
  // owners and its own effect archive and voice bank.
  'PlPe.dat':'PlPe.dat','PlPeAJ.dat':'PlPeAJ.dat','PlPeNr.dat':'PlPeNr.dat',
  'PlPeYe.dat':'PlPeYe.dat','PlPeWh.dat':'PlPeWh.dat','PlPeBu.dat':'PlPeBu.dat',
  'PlPeGr.dat':'PlPeGr.dat','EfPeData.dat':'EfPeData.dat','peach.ssm':'audio/us/peach.ssm',
  'PlMt.dat':'PlMt.dat','PlMtAJ.dat':'PlMtAJ.dat','PlMtNr.dat':'PlMtNr.dat',
  'PlMtRe.dat':'PlMtRe.dat','PlMtBu.dat':'PlMtBu.dat','PlMtGr.dat':'PlMtGr.dat',
  'EfMtData.dat':'EfMtData.dat','mewtwo.ssm':'audio/us/mewtwo.ssm',
  // Game & Watch has one authored shared costume model, ten Article roots,
  // and no fighter-specific effect table in ftData_UnkBytePerCharacter.
  'PlGw.dat':'PlGw.dat','PlGwAJ.dat':'PlGwAJ.dat','PlGwNr.dat':'PlGwNr.dat',
  'GmRstMGw.dat':'GmRstMGw.dat','gw.ssm':'audio/us/gw.ssm',
  'PlKb.dat':'PlKb.dat','PlKbAJ.dat':'PlKbAJ.dat','PlKbNr.dat':'PlKbNr.dat',
  'PlKbYe.dat':'PlKbYe.dat','PlKbBu.dat':'PlKbBu.dat','PlKbRe.dat':'PlKbRe.dat',
  'PlKbGr.dat':'PlKbGr.dat','PlKbWh.dat':'PlKbWh.dat','EfKbData.dat':'EfKbData.dat',
  // Kirby's ftKb_Init_803CA9D0 source table names every copy-action DAT.
  // The five source costume-zero hat roots come from ftKb_Init_803CB3E8 and
  // its referenced Fighter_CostumeStrings rows.
  ...Object.fromEntries([
    'PlKbCpMr.dat','PlKbCpFx.dat','PlKbCpCa.dat','PlKbCpDk.dat',
    'PlKbCpKp.dat','PlKbCpLk.dat','PlKbCpSk.dat','PlKbCpNs.dat',
    'PlKbCpPe.dat','PlKbCpPp.dat','PlKbCpPk.dat','PlKbCpSs.dat',
    'PlKbCpYs.dat','PlKbCpPr.dat','PlKbCpMt.dat','PlKbCpLg.dat',
    'PlKbCpMs.dat','PlKbCpZd.dat','PlKbCpCl.dat','PlKbCpDr.dat',
    'PlKbCpFc.dat','PlKbCpPc.dat','PlKbCpGw.dat','PlKbCpGn.dat',
    'PlKbCpFe.dat','PlKbNrCpDk.dat','PlKbNrCpPr.dat','PlKbNrCpMt.dat',
    'PlKbNrCpFc.dat','PlKbNrCpGw.dat',
  ].map(name=>[name,name])),
  // Kirby's ftKb_Init_803CB46C and efAsync_DatEntries source tables name
  // these copy-specific effect banks independently of the donor's own bank.
  ...Object.fromEntries([
    'EfKbMs.dat','EfKbZd.dat','EfKbMr.dat','EfKbFx.dat','EfKbSs.dat',
    'EfKbPk.dat','EfKbLg.dat','EfKbCa.dat','EfKbDk.dat','EfKbKp.dat',
    'EfKbIc.dat','EfKbGn.dat','EfKbFe.dat',
  ].map(name=>[name,name])),
  'GmRstMKb.dat':'GmRstMKb.dat','kirby.ssm':'audio/us/kirby.ssm',
  'PlSs.dat':'PlSs.dat','PlSsAJ.dat':'PlSsAJ.dat','PlSsNr.dat':'PlSsNr.dat',
  'PlSsPi.dat':'PlSsPi.dat','PlSsBk.dat':'PlSsBk.dat','PlSsGr.dat':'PlSsGr.dat',
  'PlSsLa.dat':'PlSsLa.dat','EfSsData.dat':'EfSsData.dat','samus.ssm':'audio/us/samus.ssm',
  'GmRstMSs.dat':'GmRstMSs.dat',
  // Yoshi uses three registered Articles; x48[3] is Egg Lay's source joint.
  'PlYs.dat':'PlYs.dat','PlYsAJ.dat':'PlYsAJ.dat','PlYsNr.dat':'PlYsNr.dat',
  'PlYsRe.dat':'PlYsRe.dat','PlYsBu.dat':'PlYsBu.dat','PlYsYe.dat':'PlYsYe.dat',
  'PlYsPi.dat':'PlYsPi.dat','PlYsAq.dat':'PlYsAq.dat','EfYsData.dat':'EfYsData.dat',
  'yoshi.ssm':'audio/us/yoshi.ssm','GmRstMYs.dat':'GmRstMYs.dat',
  // Zelda and Sheik are source-owned transformation forms with separate
  // fighter/action/costume/effect/result archives and one shared voice bank.
  'PlZd.dat':'PlZd.dat','PlZdAJ.dat':'PlZdAJ.dat','PlZdNr.dat':'PlZdNr.dat',
  'PlZdRe.dat':'PlZdRe.dat','PlZdBu.dat':'PlZdBu.dat','PlZdGr.dat':'PlZdGr.dat','PlZdWh.dat':'PlZdWh.dat',
  'PlSk.dat':'PlSk.dat','PlSkAJ.dat':'PlSkAJ.dat','PlSkNr.dat':'PlSkNr.dat',
  'PlSkRe.dat':'PlSkRe.dat','PlSkBu.dat':'PlSkBu.dat','PlSkGr.dat':'PlSkGr.dat','PlSkWh.dat':'PlSkWh.dat',
  'EfZdData.dat':'EfZdData.dat','GmRstMZd.dat':'GmRstMZd.dat','GmRstMSk.dat':'GmRstMSk.dat',
  // Popo and Nana are separate source FTKinds under one selectable identity.
  'PlPp.dat':'PlPp.dat','PlPpAJ.dat':'PlPpAJ.dat','PlPpNr.dat':'PlPpNr.dat',
  'PlPpGr.dat':'PlPpGr.dat','PlPpOr.dat':'PlPpOr.dat','PlPpRe.dat':'PlPpRe.dat',
  'PlNn.dat':'PlNn.dat','PlNnAJ.dat':'PlNnAJ.dat','PlNnNr.dat':'PlNnNr.dat',
  'PlNnYe.dat':'PlNnYe.dat','PlNnAq.dat':'PlNnAq.dat','PlNnWh.dat':'PlNnWh.dat',
  'EfIcData.dat':'EfIcData.dat','GmRstMPn.dat':'GmRstMPn.dat','ice.ssm':'audio/us/ice.ssm',
  'ff_flat.hps':'audio/ff_flat.hps','ff_ice.hps':'audio/ff_ice.hps',
  'ff_kirby.hps':'audio/ff_kirby.hps','ff_samus.hps':'audio/ff_samus.hps',
  'ff_yoshi.hps':'audio/ff_yoshi.hps',
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
