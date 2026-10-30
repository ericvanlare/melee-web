// SPDX-License-Identifier: GPL-3.0-or-later
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {createCpuIntervalEvidenceSink,validateCpuLoopExports,
  validateCpuRestoreRouteSelection,recordCpuRestoreFailure,selectCpuIntervalFailureStatics,
  recordCpuIntervalFailureStatics} from './run_write_census.mjs';

test('interval evidence is complete and independently hash-bound before its receipt resolves',async()=>{
  const out=fs.mkdtempSync(path.join(os.tmpdir(),'cpu-loop-sink-'));
  const sink=createCpuIntervalEvidenceSink(out);
  const interval={status:'compared-before-retirement',raw:['AAAA','AAAB'],pre:{fields:['18446744073709551615']}};
  const receipt=await sink(0,interval);
  const bytes=fs.readFileSync(path.join(out,receipt.path));
  assert.deepEqual(JSON.parse(bytes),interval);
  assert.equal(receipt.bytes,bytes.length);
  assert.equal(receipt.sha256,crypto.createHash('sha256').update(bytes).digest('hex'));
  assert.deepEqual(fs.readdirSync(path.join(out,'intervals')),['0000-evidence.json']);
  await assert.rejects(sink(0,{replacement:true}),/EEXIST/);
  assert.deepEqual(fs.readFileSync(path.join(out,receipt.path)),bytes);
  assert.equal(fs.existsSync(path.join(out,receipt.path+'.partial')),true);
  fs.rmSync(out,{recursive:true});
});

test('failed partial interval remains readable and malformed indices cannot escape the owned directory',async()=>{
  const out=fs.mkdtempSync(path.join(os.tmpdir(),'cpu-loop-failed-sink-'));
  const sink=createCpuIntervalEvidenceSink(out);
  for(const index of [-1,1.5,0x100000000,'../escape'])await assert.rejects(sink(index,{}),/index/);
  assert.deepEqual(fs.readdirSync(path.join(out,'intervals')),[]);
  const failed={status:'failed',failure:'first raw mismatch',partial:{cpu:{bytesBase64:'AAE='}}};
  const receipt=await sink(4,failed);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(out,receipt.path))),failed);
  const circular={};circular.self=circular;
  await assert.rejects(sink(5,circular),/circular/i);
  assert.deepEqual(fs.readdirSync(path.join(out,'intervals')),['0004-evidence.json']);
  fs.rmSync(out,{recursive:true});
});

test('loop route is explicit, exclusive and requires the existing complete private preparation',()=>{
  const route={'--correct-predicted-branch-loop':true,'--aggregate-cpu-drawn-reference-seven-tick':true,
    '--aggregate-cpu-restore-config':'fixture','--slippi-lobj-presentation-cache':true,
    '--slippi-tobj-presentation-cache':true,'--slippi-particle-presentation-cache':true};
  assert.equal(validateCpuRestoreRouteSelection(route),'independent-browser-correction-loop-v1');
  for(const flag of ['--correct-predicted-branch','--correct-predicted-branch-two-window'])
    assert.throws(()=>validateCpuRestoreRouteSelection({...route,[flag]:true}),/mutually exclusive/);
  assert.throws(()=>validateCpuRestoreRouteSelection({'--correct-predicted-branch-loop':true}),/preparation/);
  const exports={enable:true,selectSpan:true,evidence:true,evidenceBytes:true};
  assert.deepEqual(validateCpuLoopExports(exports),exports);
  for(const key of Object.keys(exports))assert.throws(()=>validateCpuLoopExports({...exports,[key]:false}),/exports/);
});

test('outer failure retains interval receipts and first native failure without inventing a restore pass',()=>{
  const report={execution:{intervalEvidence:[{index:0,path:'intervals/0000-evidence.json'}]}};
  const error=new Error('retirement refused');
  error.cpuCorrectedLoopPartial={status:'failed',failure:String(error),intervals:[{index:0,retirement:'refused'}]};
  recordCpuRestoreFailure(report,error);
  assert.equal(report.execution.correctedLoop,error.cpuCorrectedLoopPartial);
  assert.equal(report.execution.intervalEvidence.length,1);
  assert.equal(report.execution.cpuOuterValidation,'failed');
  assert.match(report.execution.restorePerformed,/unknown-on-failure/);
});

test('first interval effect diagnostics require exact producer rows and only copy raw failure bytes',async()=>{
  const rows=[['failure',4,16],['epoch',8,24]].map(([name,size,address])=>({
    source:'src/gameplay_cpu_interval.c',input_object:'libgameplay_runtime.a(gameplay_cpu_interval.c.o)',
    input_section:`.bss.${name}`,size,address}));
  const manifest={mutable_input_sections:rows};
  const selected=selectCpuIntervalFailureStatics(manifest);
  for(const bad of [rows.slice(1),[...rows,rows[0]],rows.map((r,i)=>i? r:{...r,size:8}),
    rows.map((r,i)=>i?r:{...r,input_object:'other.a(gameplay_cpu_interval.c.o)'}),
    rows.map((r,i)=>i?r:{...r,address:0xffffffff})])
    assert.throws(()=>selectCpuIntervalFailureStatics({mutable_input_sections:bad}),/manifest row refused/);
  const heap=new Uint8Array(64);heap[16]=3;heap[24]=7;
  const before=heap.slice(),report={};
  const previous=globalThis.window;
  try{
    globalThis.window={__slippiSynctest:{module:{HEAPU8:heap}}};
    const page={evaluate:async(fn,arg)=>fn(arg)};
    await recordCpuIntervalFailureStatics(report,page,selected,'reference');
    assert.deepEqual(report.cpuIntervalFailureStatics.reference.map(r=>r.bytes),[[3,0,0,0],[7,0,0,0,0,0,0,0]]);
    assert.deepEqual(heap,before);
    await recordCpuIntervalFailureStatics(report,page,[{name:'outside',address:63,size:4}],'predicted');
    assert.match(report.cpuIntervalFailureStatics.predicted.error,/outside current Wasm memory/);
    assert.deepEqual(report.cpuIntervalFailureStatics.reference[0].bytes,[3,0,0,0]);
  }finally{if(previous===undefined)delete globalThis.window;else globalThis.window=previous;}
});
