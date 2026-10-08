"""Actual observer/helper controls with explicit synthetic Fighter/player headers.

These fixtures do not execute original gameplay, allocation, RNG draws or graphics.
Owned scratch is external and retained on a failing control.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
HEADERS=r"""
#ifndef HIT_PROBE_SYNTHETIC_TYPES
#define HIT_PROBE_SYNTHETIC_TYPES
#include <stdint.h>
#include <stddef.h>
typedef struct HSD_GObj { void* user_data; } HSD_GObj;
typedef struct Vec3 { float x,y,z; } Vec3;
enum { FTKIND_KOOPA=5, Gm_PKind_Cpu=1, Gm_PKind_NA=3 };
typedef struct Fighter {
 HSD_GObj* gobj; unsigned player_id; int kind,motion_id,anim_id;
 unsigned x221F_b3,x2219_b1; Vec3 x8c_kb_vel;
 struct { float x1830_percent,x1838_percentTemp; int x183C_applied;
 HSD_GObj* x1868_source; int x18c4_source_ply,x18ac_time_since_hit;
 float x195c_hitlag_frames,x18a0; } dmg;
} Fighter;
typedef struct StaticPlayer { HSD_GObj* player_entity[2]; } StaticPlayer;
extern StaticPlayer test_players[4];
extern Fighter test_fighters[4];
extern HSD_GObj test_entities[4];
extern uint32_t* seed_ptr;
StaticPlayer* Player_GetPtrForSlot(unsigned slot);
int Player_GetPlayerSlotType(unsigned slot);
#endif
"""
class HitTransitionProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch=Path(tempfile.mkdtemp(prefix="hit-transition-control-",dir=os.environ.get("TMPDIR")))
        for name in ["melee/ft/types.h","melee/pl/player.h","melee/gm/forward.h",
                     "sysdolphin/baselib/gobj.h","sysdolphin/baselib/random.h"]:
            path=cls.scratch/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(HEADERS)
        # Compile the actual accessor body, adapting only its private tracker globals.
        retail=(ROOT/"src/gameplay_retail_state.c").read_text()
        body=retail[retail.index("int melee_web_retail_primary_identity("):]
        identity=cls.scratch/"identity.c"
        identity.write_text('#include <melee/ft/types.h>\n#include <limits.h>\n'
            'static HSD_GObj* observed_entities[4];\nstatic uint32_t entity_generations[4];\n'
            'static uint32_t observed_match=UINT_MAX;\n'
            'void test_tracker_init(void){for(unsigned i=0;i<4;i++)observed_entities[i]=&test_entities[i];'
            'observed_match=0;}\nvoid test_tracker_bump(void){entity_generations[1]++;}\n'+body)
        cls.binary=cls.scratch/"actual-observer"
        compile_result=subprocess.run([shutil.which("cc") or "cc","-std=c11","-Wall","-Wextra","-Werror",
            "-DMELEE_WEB_RNG_DRAW_OBSERVER=1","-DMELEE_WEB_HIT_PROBE_SYNTHETIC=1",
            "-I"+str(cls.scratch),"-I"+str(ROOT/"src"),
            str(ROOT/"src/gameplay_hit_transition_probe.c"),
            str(ROOT/"tests/native_hit_transition_probe_fixture.c"),str(identity),
            "-o",str(cls.binary)],capture_output=True,text=True)
        (cls.scratch/"compile.log").write_text(compile_result.stdout+compile_result.stderr)
        if compile_result.returncode:raise RuntimeError("Synthetic compile failed; retained "+str(cls.scratch))
        cls.failed=False
    def run(self, result=None):
        result=super().run(result)
        self.__class__.test_result=result
        return result
    @classmethod
    def tearDownClass(cls):
        result=getattr(cls,"test_result",None)
        outcomes=[] if result is None else result.failures+result.errors
        failed=any(isinstance(getattr(test,"test_case",test),cls) for test,_ in outcomes)
        if not cls.failed and not failed:shutil.rmtree(cls.scratch)
    def run_mode(self,mode,valid=True):
        result=subprocess.run([str(self.binary),mode],capture_output=True,text=True)
        (self.scratch/(mode+".stdout")).write_text(result.stdout)
        (self.scratch/(mode+".stderr")).write_text(result.stderr)
        if valid:
            if result.returncode: self.__class__.failed=True
            self.assertEqual(result.returncode,0,result.stderr)
            return [json.loads(line) for line in result.stdout.splitlines()]
        if result.returncode==0:self.__class__.failed=True
        self.assertNotEqual(result.returncode,0)
    def test_zero_hooks_still_complete(self):
        rows=self.run_mode("zero")
        self.assertEqual([r["source_cursor"] for r in rows],[5238,5239,5240])
        for row in rows:
            self.assertEqual(row["hook_counts"],[0,0,0])
            self.assertEqual([e["phase"] for e in row["events"]],["scheduler_start","scheduler_return"])
    def test_actual_ordered_logs_and_multiple_motion_ordinals(self):
        rows=self.run_mode("log20")
        for row in rows:
            self.assertEqual(row["hook_counts"],[1,1,2])
            logs=[e for e in row["events"] if e["phase"]=="log"]
            self.assertEqual([e["log_index"] for e in logs],list(range(20)))
            self.assertTrue(all(e["source_slot"]==0 for e in logs))
            self.assertEqual(logs[0]["position_bits"],["3f800000","80000000","40400000"])
            self.assertEqual([e["invocation"] for e in row["events"] if e["kind"]==2 and e["phase"]=="entry"],[3,4])
            self.assertTrue(all(e["x18a0_bits"]=="80000000" for e in row["events"] if e["phase"]!="log"))
        # Exercise the actual JS artifact validator against the actual C output.
        result=subprocess.run(["node","--input-type=module","-e",
            "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
            "let text='';for await(const chunk of process.stdin)text+=chunk;"
            "const rows=JSON.parse(text).map(JSON.stringify);"
            "const r=validateHitTransitionProbeRows(rows,parseHitTransitionProbe('5238,5239,5240'),{observedCursor:5240});"
            "if(!r.complete)throw Error('incomplete');"],
            cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
        if result.returncode:self.__class__.failed=True
        self.assertEqual(result.returncode,0,result.stderr)
    def test_unmatched_source_remains_unattributed(self):
        for row in self.run_mode("unattributed"):
            self.assertTrue(all(e["source_slot"]==-1 for e in row["events"] if e["phase"]=="log"))
    def test_disabled_and_unselected_do_not_inspect_objects(self):
        self.assertEqual(self.run_mode("disabled"),[])
        self.assertEqual(self.run_mode("unselected"),[])
    def test_actual_accessor_rejects_invalid_identity(self):self.assertEqual(self.run_mode("accessor"),[])
    def test_generation_changes_fail(self):self.run_mode("generation_changed",False)
    def test_wrong_player(self):self.run_mode("wrong_player",False)
    def test_follower_relationship(self):self.run_mode("follower",False)
    def test_js_selector_validator_and_installed_consumer(self):
        result=subprocess.run(["node",str(ROOT/"tests/hit_transition_probe_test.mjs")],
            cwd=ROOT,capture_output=True,text=True)
        if result.returncode:self.__class__.failed=True
        self.assertEqual(result.returncode,0,result.stderr)
    def test_invalid_relationship(self):self.run_mode("bad_identity",False)
    def test_invalid_authored_count(self):self.run_mode("count21",False)
    def test_invalid_log_order(self):self.run_mode("log_gap",False)
    def test_unmatched_return(self):self.run_mode("wrong_return",False)
    def test_duplicate_return(self):self.run_mode("duplicate_return",False)
    def test_missing_hook_return(self):self.run_mode("missing_return",False)
    def test_missing_scheduler_completion(self):self.run_mode("missing_scheduler",False)
    def test_diagnostic_storage_overflow(self):self.run_mode("overflow",False)

if __name__=="__main__":unittest.main()
