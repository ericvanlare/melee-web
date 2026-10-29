import test from 'node:test';
import assert from 'node:assert/strict';
import {Blob, File} from 'node:buffer';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {openDiscImage, discFileTransferInfo} from '../web/disc-image.mjs';
import {openDiscSession} from '../web/disc-session.mjs';
if (typeof globalThis.File !== 'function') globalThis.File = File;

function put32(bytes, at, value) {new DataView(bytes.buffer).setUint32(at, value);}
function fixture({missingTail = false, dol = null} = {}) {
  const offset = dol ? 0x500000 : 0x1000;
  const size = 33, extent = 64;
  const image = new Uint8Array(offset + (missingTail ? size : extent));
  image.set(new TextEncoder().encode('GALE01\0\x02'));
  put32(image, 0x1c, 0xc2339f3d);
  const names = new TextEncoder().encode('file.dat\0');
  const fst = new Uint8Array(24+names.length);
  put32(fst, 0, 0x01000000);put32(fst, 8, 2);
  put32(fst, 12, 0);put32(fst, 16, offset);put32(fst, 20, size);fst.set(names, 24);
  put32(image, 0x424, 0x800);put32(image, 0x428, fst.length);image.set(fst, 0x800);
  if (dol) {put32(image, 0x420, 0x1000);image.set(dol, 0x1000);}
  for (let i=0;i<size;++i) image[offset+i] = i+1;
  if (!missingTail) for (let i=size;i<extent;++i) image[offset+i] = 0x80+i;
  return {image,offset,size,extent};
}

test('actual DiscImage preserves logical bytes and nonzero FST transfer tail separately', async () => {
  const f=fixture(),disc=await openDiscImage(new Blob([f.image]));
  const logical=await disc.readFile('file.dat'),transfer=await disc.readFileTransfer('file.dat');
  assert.equal(logical.length,33);assert.equal(transfer.length,64);
  assert.deepEqual(logical,transfer.slice(0,33));
  assert.deepEqual(transfer.slice(33),f.image.slice(f.offset+33));
  assert.ok(transfer.slice(33).every(x=>x!==0));
  assert.deepEqual(await disc.readFile('file.dat'),logical);
  await assert.rejects(disc.readFile('file.dat',0,64),/outside the disc file/);
});

test('actual DiscImage refuses missing physical tail instead of zero filling it', async () => {
  const f=fixture({missingTail:true}),disc=await openDiscImage(new Blob([f.image]));
  assert.equal((await disc.readFile('file.dat')).length,33);
  await assert.rejects(disc.readFileTransfer('file.dat'),/tail is outside the disc/);
});

test('transfer metadata does not expand logical size and cannot exceed actual disc geometry', () => {
  const info=discFileTransferInfo({path:'file.dat',offset:100,size:33},164);
  assert.deepEqual(info,{name:'file.dat',size:33,transferSize:64});
  assert.ok(Object.isFrozen(info));
  for (const [entry,size] of [[{offset:100,size:33},163],[{offset:-1,size:33},164],
    [{offset:100,size:Number.MAX_SAFE_INTEGER},164]]) {
    assert.throws(()=>discFileTransferInfo(entry,size));
  }
});

test('actual transfer reader rejects out-of-extent and ambiguous path requests', async () => {
  const f=fixture(),disc=await openDiscImage(new Blob([f.image]));
  for (const [name,offset,size] of [['file.dat',0,65],['file.dat',64,1],['file.dat',-1,32],
    ['file.dat',0,NaN],['../file.dat',0,32],['missing.dat',0,32]])
    await assert.rejects(disc.readFileTransfer(name,offset,size));
  assert.deepEqual(await disc.readFileTransfer('file.dat',33,31),f.image.slice(f.offset+33));
});

const dolPath=process.env.MELEE_DISC_TRANSFER_DOL;
test('actual validated DiscAssetSession transports exact tail with logical/archive bytes unchanged',
  {skip: !dolPath ? 'optional pinned retail DOL fixture absent' : false}, async () => {
  const dol=readFileSync(dolPath);
  assert.equal(createHash('sha1').update(dol).digest('hex'),'08e0bf20134dfcb260699671004527b2d6bb1a45');
  const f=fixture({dol}),session=await openDiscSession(new File([f.image],'synthetic-transfer.iso'));
  const legacy=[];for await (const row of session.streamScope({logical:'file.dat'})) legacy.push(row);
  assert.equal(legacy[0].length,2);
  const transported=[];for await (const row of session.streamScope({logical:'file.dat'},{dvdTransfer:true})) transported.push(row);
  const [name,bytes,meta]=transported[0];assert.equal(name,'logical');
  assert.deepEqual(bytes,legacy[0][1]);assert.equal(meta.logicalSize,33);assert.equal(meta.transferSize,64);
  assert.deepEqual(meta.tail,f.image.slice(f.offset+33));
  assert.deepEqual((await session.readScope({logical:'file.dat'},{dvdTransfer:true})).get('logical'),bytes);
  const missing=fixture({dol,missingTail:true}),truncated=await openDiscSession(new File([missing.image],'missing-tail.iso'));
  const next=truncated.streamScope({logical:'file.dat'},{dvdTransfer:true});
  await assert.rejects(next.next(),/tail is outside the disc/);
  truncated.close();session.close();
  await assert.rejects(session.readFileTransfer('file.dat'),/closed/);
});

function actualPutNow() {
  const source=readFileSync(new URL('../web/melee-runtime.mjs',import.meta.url),'utf8');
  const begin=source.indexOf('  function putNow('),end=source.indexOf('  async function put(',begin);
  assert.ok(begin>=0&&end>begin);
  const calls=[],heap=new Uint8Array(1024);let pointer=32;
  const Module={HEAPU8:heap,_malloc(n){const at=pointer;pointer+=n;calls.push(['malloc',at,n]);return at;},
    _free(p){calls.push(['free',p]);},
    _melee_web_native_asset_file(...args){calls.push(['logical',...args]);return 1;},
    _melee_web_native_menu_file(...args){calls.push(['legacy',...args]);return 1;},
    _melee_web_native_asset_file_transfer(...args){calls.push(['transfer',...args]);return 1;}};
  const put=new Function('Module','check',`let hasLocalData=false;${source.slice(begin,end)};return putNow;`)(Module,x=>{if(!x)throw Error('native refusal');});
  return {put,Module,calls};
}

test('actual runtime putNow forwards logical bytes and nonzero tail through new guarded export', () => {
  const {put,Module,calls}=actualPutNow(),logical=new Uint8Array(33).fill(7),tail=new Uint8Array(31).fill(0xd3);
  put('file.dat',logical,9,{logicalSize:33,transferSize:64,tail});
  const sent=calls.find(x=>x[0]==='transfer');assert.ok(sent);
  assert.equal(sent[1],9);assert.equal(sent[4],33);assert.equal(sent[6],31);
  assert.deepEqual(Module.HEAPU8.slice(sent[3],sent[3]+33),logical);
  assert.deepEqual(Module.HEAPU8.slice(sent[5],sent[5]+31),tail);
  assert.equal(calls.filter(x=>x[0]==='free').length,3);
  assert.ok(!calls.some(x=>x[0]==='logical'));
});

test('actual runtime refuses missing capability or malformed tail and preserves legacy logical imports', () => {
  for(const metadata of [{logicalSize:33,transferSize:64,tail:new Uint8Array(30)},
    {logicalSize:32,transferSize:64,tail:new Uint8Array(31)}]){
    const {put,calls}=actualPutNow();assert.throws(()=>put('file.dat',new Uint8Array(33),9,metadata));
    assert.equal(calls.length,0);
  }
  const {put,Module,calls}=actualPutNow();delete Module._melee_web_native_asset_file_transfer;
  assert.throws(()=>put('file.dat',new Uint8Array(33),9,{logicalSize:33,transferSize:64,tail:new Uint8Array(31)}),/cannot preserve/);
  assert.equal(calls.filter(x=>x[0]==='free').length,2);
  assert.ok(!calls.some(x=>x[0]==='logical'));
  const legacy=actualPutNow();legacy.put('file.dat',new Uint8Array(33),9);
  assert.ok(legacy.calls.some(x=>x[0]==='logical'));
});
