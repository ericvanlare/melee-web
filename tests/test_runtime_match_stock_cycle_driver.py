"""Reduced orchestration control for the actual full-match stock driver."""

from __future__ import annotations

import shutil
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DRIVER_PATH = ROOT / "scripts/runtime_match_history_pair_browser.mjs"


def _extract_async_arrow(source: str, name: str) -> str | None:
    start = source.find(f"const {name}=")
    if start < 0:
        return None
    body = source.find("=>{", start)
    if body < 0:
        raise AssertionError(f"{name} is not an async arrow with a block body")
    opening = body + 2
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                if end < len(source) and source[end] == ";":
                    end += 1
                return source[start:end]
    raise AssertionError(f"unterminated function {name}")


class FullMatchStockCycleDriverTests(unittest.TestCase):
    def test_actual_driver_runs_three_cycles_and_refuses_bad_evidence(self):
        node = shutil.which("node")
        if node is None:
            self.skipTest("Node.js is required for the extracted source control")
        source = DRIVER_PATH.read_text(encoding="utf-8")
        driver = _extract_async_arrow(source, "driveFullMatchStockCycles")
        self.assertIsNotNone(driver, "the source must retain the actual stock-cycle driver")
        sibling = _extract_async_arrow(source, "startWalkAfterGrounded") or ""

        program = r'''
import assert from 'node:assert/strict';
const P1_LEFT=Buffer.from([0,0,129,0,0,0,0,0,0,0,0]);
const P1_RIGHT=Buffer.from([0,0,127,0,0,0,0,0,0,0,0]);
function sample(frame,stocks,{motion=14,groundAir=0,facing=1,x=4,y=5}={}){
  return {kind:'post-present-idle',frame,players:[
    {stocks,fighter:0,motion,groundAir,facing_direction:facing,x,y},
    {stocks:4,fighter:0,motion:14,groundAir:0,facing_direction:1,x:0,y:0},
  ]};
}
function poll(sourceFrame,pad,ticketId){
  const raw=Buffer.alloc(44);Buffer.from(pad).copy(raw,0);
  return {sourceFrame,localPort:0,remotePort:1,ticketId,pollSerial:String(ticketId),raw};
}
function fixture({badInitial=false,badLossInput=false,badGrounded=false}={}){
  const initial=sample(28,badInitial?3:4);
  const cycleData=[
    {before:4,after:3,lossFrame:35,platformFrame:36,groundFrame:39,
      walkPad:P1_RIGHT,platformFacing:-1,platformX:-4,groundFacing:-1,groundX:-4},
    {before:3,after:2,lossFrame:51,platformFrame:52,groundFrame:55,
      walkPad:P1_LEFT,platformFacing:1,platformX:5,groundFacing:1,groundX:0},
    {before:2,after:1,lossFrame:67,platformFrame:68,groundFrame:71,
      walkPad:P1_RIGHT,platformFacing:-1,platformX:3,groundFacing:-1,groundX:3},
  ];
  const cycles=new Map(cycleData.map((row,index)=>{
    const lossPad=badLossInput&&index===0?Buffer.alloc(11):row.walkPad;
    return [row.before,{
      loss:{sample:sample(row.lossFrame,row.after),poll:poll(row.lossFrame-1,lossPad,100+index)},
      platform:sample(row.platformFrame,row.after,{motion:13,groundAir:1,
        facing:row.platformFacing,x:row.platformX,y:8}),
      exit:{sample:sample(row.platformFrame+1,row.after),
        poll:poll(row.platformFrame,row.platformFacing===-1?P1_LEFT:P1_RIGHT,200+index)},
      grounded:sample(row.groundFrame,row.after,{
        motion:badGrounded&&index===0?13:14,groundAir:0,
        facing:row.groundFacing,x:row.groundX,y:9}),
    }];
  }));
  const events=[],proof={};
  const api={
    waitForFullMatchWalkStart:()=>initial,
    waitForFullMatchStockLoss:stock=>cycles.get(stock).loss,
    resumeFullMatchWalkStart:()=>events.push(['resume-walk-start']),
    resumeFullMatchGroundedRespawn:stock=>events.push(['resume-grounded',stock]),
    resumeFullMatchStockLoss:stock=>events.push(['resume-stock-loss',stock]),
    waitForFullMatchPlatform:stock=>cycles.get(stock+1).platform,
    resumeFullMatchPlatform:stock=>events.push(['resume-platform',stock]),
    waitForFullMatchPlatformExit:stock=>cycles.get(stock+1).exit,
    resumeFullMatchPlatformExit:stock=>events.push(['resume-platform-exit',stock]),
    waitForFullMatchGroundedRespawn:stock=>cycles.get(stock+1).grounded,
  };
  globalThis.__meleeRuntimePacedHistory=api;
  const alpha={keyToRelease:null,page:{
    evaluate:async(fn,arg)=>fn(arg),
    keyboard:{
      down:key=>events.push(['down',key]),
      up:key=>events.push(['up',key]),
    },
  }};
  const awaitGate=async promise=>promise;
  /* The extracted browser function is being run with source-shaped fake gates;
   * these samples do not represent gameplay or renderer execution. */
  @@SIBLING@@
  @@DRIVER@@
  return {run:driveFullMatchStockCycles,alpha,events,proof};
}

const success=fixture();
assert.equal(await success.run(),null,'the real driver keeps its null success return');
const proof=success.proof.full_match_stockouts;
assert.deepEqual(proof.cycles.map(row=>[row.stock_before,row.stock_after]),[[4,3],[3,2],[2,1]]);
assert.equal(proof.walks.length,3);
assert.equal(proof.platform_exits.length,3);
assert.equal(proof.final_walkoff.stock_before,1);
assert.equal(success.alpha.keyToRelease,'KeyD');
assert.deepEqual(success.events.filter(row=>row[0]==='down'||row[0]==='up'),[
  ['down','KeyD'],['up','KeyD'],['down','KeyA'],['up','KeyA'],['down','KeyA'],
  ['up','KeyA'],['down','KeyD'],['up','KeyD'],['down','KeyD'],['up','KeyD'],
  ['down','KeyA'],['up','KeyA'],['down','KeyD'],
]);
assert.deepEqual(success.events.filter(row=>row[0].startsWith('resume-')),[
  ['resume-walk-start'],['resume-stock-loss',4],['resume-platform',3],
  ['resume-platform-exit',3],['resume-grounded',3],['resume-stock-loss',3],
  ['resume-platform',2],['resume-platform-exit',2],['resume-grounded',2],
  ['resume-stock-loss',2],['resume-platform',1],['resume-platform-exit',1],
  ['resume-grounded',1],
]);
for(const options of [{badInitial:true},{badLossInput:true},{badGrounded:true}]){
  const refusal=fixture(options);
  await assert.rejects(refusal.run(),error=>error instanceof assert.AssertionError,
    `source driver should refuse invalid stock-cycle evidence: ${JSON.stringify(options)}`);
}
console.log('stock-cycle-source-control: passed; mocked orchestration only');
'''.replace("@@SIBLING@@", sibling or "").replace("@@DRIVER@@", driver or "")

        result = subprocess.run([node, "--input-type=module", "-e", program],
                                cwd=ROOT, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("stock-cycle-source-control: passed; mocked orchestration only", result.stdout)

    def test_actual_results_entry_requires_the_source_published_winner(self):
        node = shutil.which("node")
        if node is None:
            self.skipTest("Node.js is required for the extracted source control")
        source = DRIVER_PATH.read_text(encoding="utf-8")
        start = source.index("function validatePublishedFullMatchResult(")
        end = source.index("async function finishFullMatchResultsToCss(", start)
        functions = source[start:end]
        program = r'''import assert from 'node:assert/strict';
const final={sourceFrame:1123,outcome:2,winner:-1,randomSeed:11};
const sourceMatch={leg:'vs',complete:true,observed_player_source_slots:[0,1],
  players:[{human:true},{human:true}],frame:1123,rng:22,outcome:2,winner:-1,
  terminal:{outcome:2,winners:[1]}};
let sample,events;
const item={role:'alpha',page:{waitForFunction:async()=>{
  events.push('results-ready');
  return {jsonValue:async()=>true,dispose:async()=>events.push('dispose')};
}}};
const observeRuntimeOwner=async(page,retention,label,options)=>{
  assert.equal(page,item.page);
  assert.deepEqual(options,{includeMatch:true});
  events.push(label);
  return sample;
};
@@FUNCTIONS@@
sample={state:{phase:8},match:structuredClone(sourceMatch)};events=[];
const result=await readFullMatchResultsEntry(item,Date.now()+1000,final);
assert.deepEqual(result.published,{outcome:2,winners:[1]});
assert.deepEqual(result.observation,sourceMatch);
assert.deepEqual(events,['results-ready','dispose','published-original-match-result']);
for(const corrupt of [
  row=>{delete row.match.terminal;},
  row=>{row.match.terminal.winners=[0];},
  row=>{row.match.terminal.outcome=1;},
  row=>{row.match.frame++;},
  row=>{row.state.phase=7;},
]){
  sample={state:{phase:8},match:structuredClone(sourceMatch)};events=[];
  corrupt(sample);
  await assert.rejects(readFullMatchResultsEntry(item,Date.now()+1000,final),
    error=>error instanceof assert.AssertionError);
}
console.log('published-match-end-source-control: passed; modeled Results observation');
'''.replace("@@FUNCTIONS@@", functions)
        result = subprocess.run([node, "--input-type=module", "-e", program],
                                cwd=ROOT, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("published-match-end-source-control: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
