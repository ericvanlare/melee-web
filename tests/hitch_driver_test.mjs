import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {verifyServedArtifacts,stopTrace,remainingTimeout} from '../scripts/run_hitch_matrix.mjs';

const bytes=Buffer.from('frozen executable');
const digest=crypto.createHash('sha256').update(bytes).digest('hex');
let served=bytes;
const server=http.createServer((req,res)=>{
  if(req.url!='/runtime.wasm'){res.writeHead(404);res.end();return;}
  res.writeHead(200);res.end(served);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try {
  const machine={url:`http://127.0.0.1:${server.address().port}/runtime.html`,artifacts:{'runtime.wasm':digest}};
  assert.deepEqual(await verifyServedArtifacts(machine),{'runtime.wasm':{sha256:digest,bytes:bytes.length}});
  served=Buffer.from('different executable');
  await assert.rejects(verifyServedArtifacts(machine),/Served artifact changed/);
  await assert.rejects(verifyServedArtifacts({...machine,artifacts:{'missing.wasm':digest}}),/HTTP 404/);
  await assert.rejects(verifyServedArtifacts(machine,Date.now()-1),/wall-time bound/);
  assert.throws(()=>remainingTimeout(Date.now()-1),/wall-time bound/);
  assert.equal(remainingTimeout(Date.now()+10000,500),500);
} finally {await new Promise(resolve=>server.close(resolve));}

class TraceSession extends EventEmitter {
  constructor(chunks,loss=false,error=false){super();this.chunks=chunks;this.loss=loss;this.error=error;this.closed=false;}
  async send(method){
    if(method==='Tracing.end') {queueMicrotask(()=>this.emit('Tracing.tracingComplete',{stream:'1',dataLossOccurred:this.loss}));return {};}
    if(method==='IO.close'){this.closed=true;return {};}
    if(method==='IO.read'){
      if(this.error)throw Error('stream failed');
      const data=this.chunks.shift()||'';return {data,eof:this.chunks.length===0};
    }
    throw Error('unexpected '+method);
  }
}
const root=await fs.mkdtemp(path.join(os.tmpdir(),'melee-hitch-driver-'));
try {
  for(const [name,session,limit,expectedBytes,complete] of [
    ['complete',new TraceSession(['abc','def']),8,6,true],
    ['capped',new TraceSession(['abc','def']),4,3,false],
    ['lost',new TraceSession(['abc'],true),8,3,false],
    ['read-error',new TraceSession([],false,true),8,0,false],
  ]) {
    const directory=path.join(root,name);await fs.mkdir(directory);
    const result=await stopTrace(session,directory,limit);
    assert.equal(result.complete,complete);
    assert.equal(session.closed,true);
    const metadata=JSON.parse(await fs.readFile(result.paths[1],'utf8'));
    assert.equal(metadata.bytes,expectedBytes);
    assert.equal((await fs.stat(result.paths[0])).size,expectedBytes);
    assert.equal(metadata.complete,complete);
    if(name==='capped')assert.equal(metadata.bytes_read,6);
    // Repeated finalization cannot replace the first preserved trace.
    await assert.rejects(stopTrace(new TraceSession(['replacement']),directory),/EEXIST/);
  }
} finally {await fs.rm(root,{recursive:true,force:true});}
console.log('Hitch driver served identity and bounded trace checks passed');
