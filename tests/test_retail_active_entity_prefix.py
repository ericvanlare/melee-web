"""Synthetic boundary controls execute the production active-clock transport."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import unittest

from test_retail_entity_prefix import ROOT, owned_scratch, synthetic_prefix
import whole_session_replay as replay


def active_prefix(count=120, finishing_batch=1):
    capture, old = synthetic_prefix()
    start = next(i for i, row in enumerate(old) if row.get("payload", {}).get("boundary") == "setup")
    rows = copy.deepcopy(old[:start+1])
    for row in rows[:2]:
        row["payload"]["entity_profile"] = replay.ENTITY_ACTIVE_PREFIX_PROFILE
    sample = old[start+1:start+5]
    ordinal = 0
    # Sixty Ready rows, then gameplay-clock values 1..60. Any finishing-batch
    # extras remain real synthetic rows; no Ready rows are removed or relabeled.
    for tick in range(count):
        pad, observation, enter, returned = copy.deepcopy(sample)
        for row in (pad, observation):
            row.update(source_tick=tick, draw_ordinal=ordinal)
        for item in observation["payload"]["slices"]:
            if item["name"] == "scene_counter":
                item["hex"] = tick.to_bytes(4, "big").hex()
            if item["name"] == "match_clock":
                raw = bytearray(0x2e)
                raw[0x24:0x28] = max(0, tick-59).to_bytes(4, "big")
                item.update(address=0x8046b6a0, size=len(raw), hex=raw.hex())
        rows.extend((pad, observation))
        if tick < count-finishing_batch or tick == count-1:
            for row in (enter, returned):
                row.update(source_tick=tick+1, draw_ordinal=ordinal)
            rows.extend((enter, returned))
            ordinal += 1
    end = copy.deepcopy(old[-1])
    end.update(source_tick=count, draw_ordinal=ordinal)
    end["payload"].update(observed_source_ticks=count, comparison_source_ticks=count,
                          comparison_active_clock_ticks=60,
                          observed_active_clock_advances=count-60)
    rows.append(end)
    for seq, row in enumerate(rows):
        row["seq"] = seq
    capture["frames"] = capture["frames"][:2]
    pads = [row for row in rows[start+1:] if row.get("payload", {}).get("boundary") == "pad_consume"]
    for tick, pad in enumerate(pads):
        capture["frames"].append({"index":tick+2, "scene_code":3, "source_tick":tick,
                                  "pads":replay._consumed_ports(pad, pad["seq"])})
    capture["spans"][-1]["last_frame"] = count+1
    return capture, rows


class RetailActiveEntityPrefixTests(unittest.TestCase):
    def test_actual_encoder_retains_ready_and_all_active_rows(self):
        capture, rows = active_prefix()
        payload, metadata = replay.encode_v8_entity_prefix(capture, rows)
        self.assertEqual(int.from_bytes(payload[22:24], "big"), 2)
        self.assertEqual(metadata["diagnostic_prefix"], replay.ENTITY_ACTIVE_PREFIX_PROFILE)
        self.assertEqual(metadata["comparison_source_ticks"], 120)
        self.assertEqual(metadata["comparison_active_clock_ticks"], 60)
        self.assertEqual(metadata["observed_active_clock_advances"], 60)
        self.assertEqual(metadata["first_gameplay_clock"], 0)
        self.assertEqual(metadata["last_gameplay_clock"], 60)
        self.assertFalse(metadata["whole_session_equivalent"])
        selected = replay.entity_prefix_interval(rows, active_clock=True)
        self.assertEqual(len(selected["source_rows"]), 120)
        self.assertEqual(len(selected["pad_rows"]), 120)
        old_capture, old_rows = synthetic_prefix()
        old_payload, old_metadata = replay.encode_v8_entity_prefix(old_capture, old_rows)
        self.assertEqual(int.from_bytes(old_payload[22:24], "big"), 1)
        self.assertEqual(old_metadata["source_observations"], 60)
        self.assertNotIn("comparison_active_clock_ticks", old_metadata)

    def test_actual_selector_retains_extra_batch_and_rejects_browser_batching(self):
        capture, rows = active_prefix(124, 5)
        selected = replay.entity_prefix_interval(rows, active_clock=True)
        self.assertEqual(selected["draw_batches"], [1]*119+[5])
        self.assertEqual(selected["observed_source_ticks"], 124)
        self.assertEqual(selected["observed_active_clock_advances"], 64)
        with self.assertRaisesRegex(ValueError, "batching is unsupported"):
            replay.encode_v8_entity_prefix(capture, rows)
        # The selector cannot continue past the first qualifying draw or admit
        # a clock discontinuity, fabricated footer, or foreign mode.
        for mutation in ("clock_gap", "initial_clock", "clock_reverse", "clock_owner", "footer", "profile"):
            with self.subTest(mutation=mutation):
                capture, rows = active_prefix()
                if mutation in ("clock_gap", "initial_clock", "clock_reverse", "clock_owner"):
                    ticks = [r for r in rows if r.get("payload", {}).get("boundary") == "source_tick"]
                    row = ticks[0 if mutation == "initial_clock" else 62]
                    item = next(s for s in row["payload"]["slices"] if s["name"] == "match_clock")
                    if mutation == "clock_owner":
                        item["address"] += 4
                    else:
                        raw = bytearray.fromhex(item["hex"])
                        raw[0x24:0x28] = (2 if mutation == "initial_clock" else 9 if mutation == "clock_gap" else 0).to_bytes(4,"big")
                        item["hex"] = raw.hex()
                elif mutation == "footer":
                    rows[-1]["payload"]["observed_active_clock_advances"] = 59
                else:
                    rows[1]["payload"]["entity_profile"] = replay.ENTITY_PREFIX_PROFILE
                with self.assertRaises(ValueError):
                    replay.encode_v8_entity_prefix(capture, rows)

    def test_actual_native_first_qualifying_draw_and_finite_cap(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler)
        source = (ROOT/"reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        tracker = source[source.index("struct ActiveEntityPrefixBoundaryProgress"):
                         source.index("enum class SliceTag")]
        harness = r'''
#include <cassert>
#include <cstdint>
using u32=uint32_t;using u8=uint8_t;
enum class Boundary{SourceTick,DrawEnter,DrawReturn,Setup};
''' + tracker + r'''
int main(){
 ActiveEntityPrefixBoundaryProgress p;
 for(unsigned t=0;t<119;++t){
  assert(p.Observe(Boundary::SourceTick,t,5,t<60?0:t-59)==0);
  assert(p.Observe(Boundary::DrawEnter,t+1,5)==0);
  assert(p.Observe(Boundary::DrawReturn,t+1,5)==0);
 }
 for(unsigned t=119;t<124;++t)assert(p.Observe(Boundary::SourceTick,t,5,t-59)==0);
 assert(!p.qualified&&p.observations==124&&p.active_advances==64);
 assert(p.Observe(Boundary::DrawEnter,124,5)==0);
 assert(p.Observe(Boundary::DrawReturn,124,5)==1);
 assert(p.Observe(Boundary::SourceTick,124,5,65)==-1);
 ActiveEntityPrefixBoundaryProgress stalled;
 for(unsigned t=0;t<600;++t){
  assert(stalled.Observe(Boundary::SourceTick,t,5,0)==0);
  assert(stalled.Observe(Boundary::DrawEnter,t+1,5)==0);
  assert(stalled.Observe(Boundary::DrawReturn,t+1,5)==(t==599?-1:0));
 }
 ActiveEntityPrefixBoundaryProgress finishing;
 for(unsigned t=0;t<599;++t){
  assert(finishing.Observe(Boundary::SourceTick,t,5,t<544?0:t-543)==0);
  assert(finishing.Observe(Boundary::DrawEnter,t+1,5)==0);
  assert(finishing.Observe(Boundary::DrawReturn,t+1,5)==0);
 }
 for(unsigned t=599;t<604;++t)assert(finishing.Observe(Boundary::SourceTick,t,5,t-543)==0);
 assert(finishing.Observe(Boundary::SourceTick,604,5,61)==-1);
 assert(finishing.Observe(Boundary::DrawEnter,604,5)==0);
 assert(finishing.Observe(Boundary::DrawReturn,604,5)==1);
 for(unsigned m=0;m<5;++m){ActiveEntityPrefixBoundaryProgress q;
  if(m==0)assert(q.Observe(Boundary::SourceTick,0,5,1)==-1);
  if(m==1)assert(q.Observe(Boundary::SourceTick,1,5,0)==-1);
  if(m==2)assert(q.Observe(Boundary::SourceTick,0,4,0)==-1);
  if(m==3)assert(q.Observe(Boundary::DrawReturn,0,5)==-1);
  if(m==4){assert(q.Observe(Boundary::SourceTick,0,5,0)==0);assert(q.Observe(Boundary::SourceTick,1,5,2)==-1);}
 }
}
'''
        with owned_scratch("active-prefix-native-") as path:
            (path/"tracker.cpp").write_text(harness)
            result = subprocess.run([compiler,"-std=c++17","-Wall","-Werror",str(path/"tracker.cpp"),"-o",str(path/"tracker")],capture_output=True,text=True)
            (path/"compiler.log").write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(path/"tracker")],capture_output=True,text=True)
            (path/"execution.log").write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_actual_browser_request_and_completion_gate_bind_named_interval(self):
        node = shutil.which("node")
        self.assertIsNotNone(node)
        capture, rows = active_prefix()
        payload, _ = replay.encode_v8_entity_prefix(capture, rows)
        with owned_scratch("active-prefix-browser-") as path:
            recipe = path/"prefix.mwrc";recipe.write_bytes(payload)
            script = '''import fs from 'node:fs';import assert from 'node:assert/strict';
import {readRequestedEntityPrefix,sessionReplayReportCompleted} from %s;
const request=readRequestedEntityPrefix(fs.readFileSync(process.argv[1]),'a'.repeat(64));
assert.equal(request.name,'jiggly-ice-mario-fox-active60-v1');assert.equal(request.observations,120);
const report={schema:'melee-web-browser-retail-replay',version:1,pass:true,failures:[],complete:false,
diagnostic_prefix:request.name,diagnostic_prefix_complete:true,whole_session_equivalent:false,
comparison_source_ticks:120,comparison_active_clock_ticks:60,mode:'state_capture',final_scene:3,
recipe_sha256:request.recipe_sha256,frames:request.frames,source_progress:{observations:120,bound_observations:120,first_source_tick:0,last_source_tick:119},
metrics:{sourceFrames:request.frames,sourceSteps:request.frames,sourceDraws:request.frames},source_match:{complete:false,outcome:null,winner:null}};
assert(sessionReplayReportCompleted(report,request));assert(!sessionReplayReportCompleted(report));
for(const delta of [{comparison_active_clock_ticks:59},{comparison_source_ticks:60},{diagnostic_prefix:'jiggly-ice-mario-fox-v1'},{complete:true},{failures:['teardown failed']}])assert(!sessionReplayReportCompleted({...report,...delta},request));
const bytes=fs.readFileSync(process.argv[1]);bytes.writeUInt32BE(9,4);assert.throws(()=>readRequestedEntityPrefix(bytes,'a'.repeat(64)));
''' % json.dumps((ROOT/"scripts/whole_session_capture_result.mjs").as_uri())
            result = subprocess.run([node,"--input-type=module","-e",script,str(recipe)],capture_output=True,text=True)
            (path/"browser.log").write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,result.stderr)
