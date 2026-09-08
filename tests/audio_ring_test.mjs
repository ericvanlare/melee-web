import assert from 'node:assert/strict';
import { AudioRing } from '../web/audio-ring.mjs';
const source=Float32Array.from({length:512},(_,i)=>(i%17-8)/16);
function drain(chunks){const q=new AudioRing(384,0);q.push(source);const result=[];for(const count of chunks){const l=new Float32Array(count),r=new Float32Array(count);q.consume(l,r);for(let i=0;i<count;i++)result.push(l[i],r[i]);}return result;}
assert.deepEqual(drain([256]),drain([17,111,128]));
const q=new AudioRing(256,128),l=new Float32Array(128),r=new Float32Array(128);
q.push(source.subarray(0,128));q.consume(l,r);assert.equal(q.available,64);assert.equal(q.underruns,0);assert(l.every(x=>x===0));
q.push(source.subarray(128,256));q.consume(l,r);assert.equal(q.available,0);assert.equal(l[0],source[0]);assert.equal(r[127],source[255]);
q.consume(l,r);assert.equal(q.underruns,128);assert(l.every(x=>x===0));
q.reset();q.push(source);assert.equal(q.push(new Float32Array(2)),false);assert.equal(q.available,256);assert.equal(q.overflows,1);
assert.equal(q.push(new Float32Array([NaN,0])),false);q.reset();assert.equal(q.available,0);
assert.throws(()=>q.push(new Float32Array([NaN,0])),RangeError);
q.push(source);q.consume(l,r);q.push(source.subarray(0,256));q.consume(l,r);assert.deepEqual(Array.from(l),Array.from(source.filter((_,i)=>i>=256&&i%2===0)));
console.log('Audio transport partitioning, stereo order, priming, wraparound and explicit underrun/overflow passed');
