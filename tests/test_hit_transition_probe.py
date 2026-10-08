"""Actual observer/helper controls with explicit synthetic Fighter/player headers.

These fixtures do not execute original gameplay, allocation, RNG draws or graphics.
Owned scratch is external and retained on a failing control.
"""
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
def _retain_reducer_evidence(name,stdout,stderr):
    evidence=os.environ.get("MELEE_HIT_REDUCER_EVIDENCE_DIR")
    if evidence:
        directory=Path(evidence);directory.mkdir(parents=True,exist_ok=True)
        (directory/(name+".stdout")).write_text(stdout)
        (directory/(name+".stderr")).write_text(stderr)

def _compile_failure(label,result,scratch,log_name):
    return RuntimeError(f"{label}; compiler log retained at {scratch/log_name}\n"
        f"Compiler stdout:\n{result.stdout}\nCompiler stderr:\n{result.stderr}")

HEADERS=r"""
#ifndef HIT_PROBE_SYNTHETIC_TYPES
#define HIT_PROBE_SYNTHETIC_TYPES
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef float Mtx[3][4];typedef float (*MtxPtr)[4];
typedef struct HSD_JObj {Mtx matrix;} HSD_JObj;
typedef struct HitVictim {void* victim;unsigned x4;} HitVictim;
enum {HitCapsule_Disabled=0,HitCapsule_Enabled=1,HurtCapsule_Intangible=2,GA_Ground=0,GA_Air=1,HitElement_Catch=8,ftCo_MS_DamageIce=325};
typedef struct HSD_GObj { void* user_data; } HSD_GObj;
typedef struct Vec3 { float x,y,z; } Vec3;
void HSD_MtxInverse(MtxPtr matrix,Mtx inverse);
void PSMTXMultVec(Mtx matrix,Vec3* input,Vec3* output);
enum { FTKIND_KOOPA=5, Gm_PKind_Cpu=1, Gm_PKind_NA=3 };
typedef struct HitCapsule {
 int state; unsigned element,x4,unk_count; float damage,scale,coll_distance; Vec3 x58,x4C,hurt_coll_pos;
 HitVictim victims_2[12];HSD_GObj* owner;
 unsigned x42_b5,x40_b2,x40_b3,hit_grabbed_victim_only,x43_b2,x43_b1,x40_b0;
} HitCapsule;
typedef struct HurtCapsule { int state; unsigned skip_update_pos; Vec3 a_pos,b_pos,a_offset,b_offset;float scale;HSD_JObj* bone;int bone_idx;} HurtCapsule;
typedef struct FighterHurtCapsule {HurtCapsule capsule;} FighterHurtCapsule;
typedef struct Fighter {
 HSD_GObj* gobj; unsigned player_id; int kind,motion_id,anim_id;
 unsigned x221F_b3,x2219_b1; Vec3 x8c_kb_vel;
 HitCapsule x914[4];FighterHurtCapsule hurt_capsules[15];unsigned hurt_capsules_len;
 int ground_or_air,x1988,x198C;unsigned x221B_b0,x221D_b6,x221B_b5,x221C_b4;
 HSD_GObj* victim_gobj;HitCapsule x1064_thrownHitbox;
 struct { float x182c_behavior,x1834,x1830_percent,x1838_percentTemp; int x183C_applied;
 int x1840,x189C_unk_num_frames,x1914;
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
        if compile_result.returncode:raise _compile_failure(
            "Synthetic compile failed",compile_result,cls.scratch,"compile.log")
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
        if mode=="overflow":
            _retain_reducer_evidence("legacy-64-overflow",result.stdout,result.stderr)
        if valid:
            if result.returncode: self.__class__.failed=True
            self.assertEqual(result.returncode,0,result.stderr)
            return [json.loads(line) for line in result.stdout.splitlines()]
        if result.returncode==0:self.__class__.failed=True
        self.assertNotEqual(result.returncode,0)
        if mode=="overflow":
            row=json.loads(result.stdout.splitlines()[-1])
            self.assertEqual(row["source_cursor"],5238)
            self.assertTrue(row["overflowed"])
            self.assertNotIn("candidate_enabled",row)
            self.assertEqual(len(row["events"]),64)
            self.assertEqual([event["sequence"] for event in row["events"]],list(range(64)))
    def test_zero_hooks_still_complete(self):
        rows=self.run_mode("zero")
        self.assertEqual([r["source_cursor"] for r in rows],[5238,5239,5240])
        for row in rows:
            self.assertEqual(row["hook_counts"],[0,0,0])
            self.assertEqual([e["phase"] for e in row["events"]],["scheduler_start","scheduler_return"])
        self.assertEqual((rows[1]["candidate_enabled"],rows[1]["candidate_passes"],
            rows[1]["candidate_pairs"]),(True,0,0))
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


def _actual_function(source, signature):
    """Extract a verified whole C function, including nested conditional bodies."""
    start=source.index(signature); opening=source.index('{',start); depth=1; pos=opening+1
    while depth:
        if source[pos]=='{':depth+=1
        elif source[pos]=='}':depth-=1
        pos+=1
    return source[start:pos]+'\n'

def _serialized_event_sizes(line):
    marker='"events":['
    position=line.index(marker)+len(marker)
    decoder=json.JSONDecoder()
    sizes=[]
    while line[position]!=']':
        _,end=decoder.raw_decode(line,position)
        sizes.append(len(line[position:end].encode('utf-8')))
        position=end
        if line[position]==',':position+=1
        else:break
    return sizes

def _replace_source_once(source, old, new, label):
    count=source.count(old)
    if count!=1:raise RuntimeError(f'{label}: expected one source anchor, found {count}')
    return source.replace(old,new,1)

def _instrument_inner_geometry(inner):
    """Add read-only snapshots after existing calculations and branches."""
    source=inner
    anchors=[
        ('broadphase_radius = (hurt_radius * broadphase_scale) + hit_radius;',
         'broadphase_radius = (hurt_radius * broadphase_scale) + hit_radius;\n    MELEE_TRACE_VALUE("broadphase_radius", broadphase_radius);'),
        ('float bound = hit_start_copy.x + broadphase_radius;',
         'float bound = hit_start_copy.x + broadphase_radius;\n        MELEE_TRACE_VALUE("x_upper_from_start", bound);'),
        ('bound = hit_end_x - broadphase_radius;',
         'bound = hit_end_x - broadphase_radius;\n        MELEE_TRACE_VALUE("x_lower_from_end", bound);'),
        ('float bound = hit_start_copy.x - broadphase_radius;',
         'float bound = hit_start_copy.x - broadphase_radius;\n        MELEE_TRACE_VALUE("x_lower_from_start", bound);'),
        ('bound = hit_end_x + broadphase_radius;',
         'bound = hit_end_x + broadphase_radius;\n        MELEE_TRACE_VALUE("x_upper_from_end", bound);'),
        ('float bound = hit_start_y + broadphase_radius;',
         'float bound = hit_start_y + broadphase_radius;\n        MELEE_TRACE_VALUE("y_upper_from_start", bound);'),
        ('bound = hit_end->y - broadphase_radius;',
         'bound = hit_end->y - broadphase_radius;\n        MELEE_TRACE_VALUE("y_lower_from_end", bound);'),
        ('float bound = hit_start_y - broadphase_radius;',
         'float bound = hit_start_y - broadphase_radius;\n        MELEE_TRACE_VALUE("y_lower_from_start", bound);'),
        ('bound = hit_end->y + broadphase_radius;',
         'bound = hit_end->y + broadphase_radius;\n        MELEE_TRACE_VALUE("y_upper_from_end", bound);'),
        ('float bound = hit_start_z + broadphase_radius;',
         'float bound = hit_start_z + broadphase_radius;\n        MELEE_TRACE_VALUE("z_upper_from_start", bound);'),
        ('bound = hit_end->z - broadphase_radius;',
         'bound = hit_end->z - broadphase_radius;\n        MELEE_TRACE_VALUE("z_lower_from_end", bound);'),
        ('float bound = hit_start_z - broadphase_radius;',
         'float bound = hit_start_z - broadphase_radius;\n        MELEE_TRACE_VALUE("z_lower_from_start", bound);'),
        ('bound += broadphase_radius;',
         'bound += broadphase_radius;\n        MELEE_TRACE_VALUE("z_upper_from_end", bound);'),
        ('closest_denom = (hit_len_sq * hurt_len_sq) - (segment_dot * segment_dot);',
         'closest_denom = (hit_len_sq * hurt_len_sq) - (segment_dot * segment_dot);\n'
         '    MELEE_TRACE_VALUE("hit_len_sq", hit_len_sq);\n'
         '    MELEE_TRACE_VALUE("hurt_len_sq", hurt_len_sq);\n'
         '    MELEE_TRACE_VALUE("segment_dot", segment_dot);\n'
         '    MELEE_TRACE_VALUE("hit_start_dot", hit_start_dot);\n'
         '    MELEE_TRACE_VALUE("hurt_start_dot", hurt_start_dot);\n'
         '    MELEE_TRACE_VALUE("closest_denom", closest_denom);'),
        ('if (approximatelyZero(hurt_len_sq)) {',
         'if (approximatelyZero(hurt_len_sq)) {\n'
         '        MELEE_TRACE_EVENT("solver_zero_hurt_length");'),
        ('    } else {\n        if (approximatelyZero(closest_denom)) {',
         '    } else {\n        MELEE_TRACE_EVENT("solver_nonzero_hurt_length");\n'
         '        if (approximatelyZero(closest_denom)) {\n'
         '            MELEE_TRACE_EVENT("solver_parallel_axes");'),
        ('        } else {\n            hit_param = ((segment_dot * hurt_start_dot) -',
         '        } else {\n            MELEE_TRACE_EVENT("solver_nonparallel_axes");\n'
         '            hit_param = ((segment_dot * hurt_start_dot) -'),
        ('    hit_closest->x = (hit_delta.x * hit_param) + hit_start_copy.x;',
         '    MELEE_TRACE_VALUE("hit_param", hit_param);\n'
         '    MELEE_TRACE_VALUE("hurt_param", hurt_param);\n'
         '    hit_closest->x = (hit_delta.x * hit_param) + hit_start_copy.x;'),
        ('    axis.x = sqrtf(closest_dist_sq);',
         '    axis.x = sqrtf(closest_dist_sq);\n'
         '    MELEE_TRACE_VALUE("closest_dist_sq", closest_dist_sq);\n'
         '    MELEE_TRACE_VALUE("axis_distance", axis.x);\n'
         '    MELEE_TRACE_VEC("hit_closest", *hit_closest);\n'
         '    MELEE_TRACE_VEC("hurt_closest", *hurt_closest);'),
        ('    HSD_MtxInverse(hurt_mtx, inv_hurt_mtx);',
         '    HSD_MtxInverse(hurt_mtx, inv_hurt_mtx);\n'
         '    MELEE_TRACE_MTX("inverse_hurt_matrix", inv_hurt_mtx);'),
        ('    PSMTXMultVec(inv_hurt_mtx, hit_closest, &hit_start_copy);',
         '    PSMTXMultVec(inv_hurt_mtx, hit_closest, &hit_start_copy);\n'
         '    MELEE_TRACE_VEC("hit_closest_local", hit_start_copy);'),
        ('    PSMTXMultVec(inv_hurt_mtx, hurt_closest, &hit_delta);',
         '    PSMTXMultVec(inv_hurt_mtx, hurt_closest, &hit_delta);\n'
         '    MELEE_TRACE_VEC("hurt_closest_local", hit_delta);'),
        ('    local_dist_sq = sqrtf(local_dist_sq);',
         '    local_dist_sq = sqrtf(local_dist_sq);\n'
         '    MELEE_TRACE_VALUE("local_distance", local_dist_sq);'),
        ('    scaled_hurt_radius = (hurt_radius * axis.x) / local_dist_sq;',
         '    scaled_hurt_radius = (hurt_radius * axis.x) / local_dist_sq;\n'
         '    MELEE_TRACE_VALUE("scaled_hurt_radius", scaled_hurt_radius);'),
        ('    contact_lerp = scaled_hurt_radius / axis.x;',
         '    contact_lerp = scaled_hurt_radius / axis.x;\n'
         '    MELEE_TRACE_VALUE("contact_lerp", contact_lerp);'),
        ('    *out_overlap = allowed_distance - axis.x;',
         '    *out_overlap = allowed_distance - axis.x;\n'
         '    MELEE_TRACE_VALUE("allowed_distance", allowed_distance);\n'
         '    MELEE_TRACE_VALUE("overlap", *out_overlap);'),
        ('    if (allowed_distance < axis.x) {\n        return false;\n    }\n    return true;',
         '    MELEE_TRACE_VEC("contact", *out_contact_pos);\n'
         '    if (allowed_distance < axis.x) {\n'
         '        MELEE_TRACE_EVENT("distance_reject");\n'
         '        return false;\n'
         '    }\n'
         '    MELEE_TRACE_EVENT("distance_accept");\n'
         '    return true;'),
    ]
    for old,new in anchors:source=_replace_source_once(source,old,new,old[:48])
    aabb_end=source.index('    // Solve closest points between the two segment axes.')
    aabb=source[:aabb_end]
    tail=source[aabb_end:]
    rejects=aabb.count('return false;')
    if rejects!=12:raise RuntimeError(f'expected 12 ordered AABB exits, found {rejects}')
    # Match original exits once; never search text containing inserted exits.
    original_exits=list(re.finditer('return false;',aabb))
    for index,match in reversed(list(enumerate(original_exits,1))):
        replacement=f'MELEE_TRACE_REJECT({index}, bound);\n            return false;'
        aabb=aabb[:match.start()]+replacement+aabb[match.end():]
    return aabb+tail

def _captured_geometry_by_key(rows):
    return {
        (row['source_cursor'],event['hit_index'],event['hurt_index']):event
        for row in rows if row.get('source_cursor')==5239
        for event in row['events'] if event['phase']=='geometry'}

class InnerGeometryGeneratorTest(unittest.TestCase):
    def test_adjacent_geometry_cannot_overwrite_selected_cursor(self):
        events=[{'phase':'geometry','hit_index':0,'hurt_index':0,'marker':cursor}
            for cursor in (5238,5239,5240)]
        rows=[{'source_cursor':cursor,'events':[event]}
            for cursor,event in zip((5238,5239,5240),events)]
        self.assertEqual(_captured_geometry_by_key(rows),{(5239,0,0):events[1]})
        self.assertEqual(_captured_geometry_by_key([rows[0],rows[2]]),{})

    def test_each_original_return_has_one_ordered_tag(self):
        lb=(ROOT/'.deps/melee/src/melee/lb/lbcollision.c').read_text()
        inner=_actual_function(lb,'bool lbColl_80006E58(')
        generated=_instrument_inner_geometry(inner)
        boundary='    // Solve closest points between the two segment axes.'
        original_aabb=inner.split(boundary,1)[0]
        generated_aabb=generated.split(boundary,1)[0]
        self.assertEqual(original_aabb.count('return false;'),12)
        sites=re.findall(r'MELEE_TRACE_REJECT\((\d+), bound\);\s*return false;',
            generated_aabb)
        self.assertEqual(sites,[str(i) for i in range(1,13)])
        self.assertEqual(generated_aabb.count('MELEE_TRACE_REJECT('),12)
        self.assertEqual(generated_aabb.count('return false;'),12)
        # Removing standalone diagnostic calls must recover every original
        # token, including each comparison and return at its original site.
        stripped=re.sub(r'^\s*MELEE_TRACE_\w+\([^\n]*\);\n','',generated,
            flags=re.MULTILINE)
        self.assertEqual(re.sub(r'\s+','',stripped),re.sub(r'\s+','',inner))

def _capture_inner_geometry(capture_path):
    selected=[]
    with Path(capture_path).open() as source:
        for line in source:
            if not line.strip():continue
            row=json.loads(line)
            if row.get('source_cursor')!=5239:continue
            for event in row.get('events',[]):
                if event.get('phase')=='geometry':selected.append(event)
    if len(selected)!=60:raise AssertionError(f'expected 60 geometry events, got {len(selected)}')
    expected=[(hit,hurt) for hit in range(4) for hurt in range(15)]
    if [(e.get('hit_index'),e.get('hurt_index')) for e in selected]!=expected:
        raise AssertionError('captured geometry cursor order differs from authored 4x15 traversal')
    records=[]
    for event in selected:
        endpoints=event.get('inner_endpoints_bits')
        matrix=event.get('inner_matrix_bits')
        arguments=event.get('inner_effective_arguments_bits')
        if event.get('inner_call_count')!=1 or event.get('inner_matrix_present') is not True:
            raise AssertionError('geometry event lacks exactly one captured actual inner call and matrix')
        if not (len(endpoints)==12 and len(matrix)==12 and len(arguments)==3):
            raise AssertionError('captured inner-call operand widths differ from the source signature')
        bits=endpoints+matrix+arguments
        if any(not re.fullmatch(r'[0-9a-f]{8}',value) for value in bits):
            raise AssertionError('captured inner-call operand is not a raw 32-bit float word')
        result=event.get('result')
        if result not in (0,1):raise AssertionError(f'invalid captured result: {result!r}')
        records.append((event['hit_index'],event['hurt_index'],result,bits))
    return records

class HitCandidateSourceTest(unittest.TestCase):
    """Patched routine controls plus an isolated captured-inner geometry reducer.

    Selector controls still use explicit synthetic services. The offline reducer
    has a separate fixture that uses the actual extracted matrix and math bodies.
    Neither path is runtime/compiler-ABI validation.
    """
    @classmethod
    def setUpClass(cls):
        HitTransitionProbeTest.setUpClass.__func__(cls)
        source_root=cls.scratch/'source'
        for relative in ('src/melee/ft/ftcoll.c','src/melee/lb/lbcollision.c'):
            target=source_root/relative;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(ROOT/'.deps/melee'/relative,target)
        applied=subprocess.run(['git','apply','--include=src/melee/ft/ftcoll.c',
            '--include=src/melee/lb/lbcollision.c',str(ROOT/'patches/melee-gameplay.patch')],
            cwd=source_root,capture_output=True,text=True)
        (cls.scratch/'apply.log').write_text(applied.stdout+applied.stderr)
        if applied.returncode:raise RuntimeError('Actual patch application failed; retained '+str(cls.scratch))
        ft=(source_root/'src/melee/ft/ftcoll.c').read_text()
        lb=(source_root/'src/melee/lb/lbcollision.c').read_text()
        lb_header=(ROOT/'.deps/melee/src/melee/lb/lbcollision.h').read_text()
        header=HEADERS+r"""
typedef unsigned char u8;typedef int s32;typedef unsigned u32;typedef int FighterKind;typedef int HitCapsuleState;typedef void* UNK_T;
typedef struct DmgLogEntry {int x0,kind;HSD_GObj* gobj;HitCapsule *hit0,*hit1;void *unk_anim0,*hurt1;Vec3 pos;int size_of_xC;float x20;} DmgLogEntry;
"""
        (cls.scratch/'candidate_source_fixture.h').write_text(header)
        signatures=('static void tiplog(', 'static inline void inlineB0(',
            'static inline HitCapsuleState checkTipLog(', 'static inline bool inlineB1(',
            'static inline bool inlineB2(', 'static inline float inlineB3(', 'bool ftColl_80076ED8(')
        bodies='static DmgLogEntry dmg_log0[20];\nstruct DmgLogEntry dmg_log1[20];\nstatic int dmg_log0_idx;\nstatic int dmg_log1_idx;\n'
        bodies+='\n'.join(_actual_function(ft,x) for x in signatures)
        bodies+=_actual_function(lb_header,'static inline bool approximatelyZero(')
        bodies+=_actual_function(lb,'float lbColl_80005EBC(')
        inner=_actual_function(lb,'bool lbColl_80006E58(')
        inner=inner.replace('bool lbColl_80006E58(','static bool melee_web_actual_inner_body(',1)
        bodies+=inner
        bodies+=_actual_function(lb,'bool lbColl_8000805C(')
        # Exact admission predicate from the verified whole normal-hurt owner.
        owner=_actual_function(ft,'void ftColl_80078C70(')
        start=owner.index('if ((temp_r23->state != HitCapsule_Disabled)')+3
        pos=start;depth=0
        while True:
            if owner[pos]=='(':depth+=1
            elif owner[pos]==')':
                depth-=1
                if not depth:break
            pos+=1
        predicate=owner[start:pos+1]
        bodies+='\nstatic int actual_admission(Fighter* this_fp,Fighter* victim_fp,HitCapsule* temp_r23,HSD_GObj* this_gobj) {return '+predicate+';}\n'

        (cls.scratch/'candidate_source_bodies.inc').write_text(bodies)
        hook_call=('    melee_web_hit_probe_geometry_inner(hit_start, hit_end, hurt_start, hurt_end,\n'
            '        hurt_mtx, hit_radius, hurt_radius, broadphase_scale);\n')
        if bodies.count(hook_call)!=1:raise RuntimeError('Expected one extracted actual inner-helper hook')
        offline_inner=_actual_function(lb,'bool lbColl_80006E58(')
        if offline_inner.count(hook_call)!=1:raise RuntimeError('Offline extraction lost the actual inner-helper hook')
        offline_inner=offline_inner.replace(hook_call,'')
        offline_inner=offline_inner.replace('bool lbColl_80006E58(',
            'static bool melee_web_offline_inner(',1)

        hsd_mtx=(ROOT/'.deps/melee/src/sysdolphin/baselib/mtx.c').read_text()
        epsilon_definitions=re.findall(r'^#define EPSILON [^\n]+$',hsd_mtx,re.MULTILINE)
        if len(epsilon_definitions)!=1:raise RuntimeError('Expected unique HSD matrix EPSILON definition')
        epsilon_definition=epsilon_definitions[0]+'\n'
        hsd_mtx_header=(ROOT/'.deps/melee/src/sysdolphin/baselib/mtx.h').read_text()
        dolphin_mtx=(ROOT/'.deps/melee/extern/dolphin/src/dolphin/mtx/mtx.c').read_text()
        ps_math=(ROOT/'src/gameplay_ps_math.c').read_text()
        offline_helpers='\n'.join((
            _actual_function(lb_header,'static inline bool approximatelyZero('),
            _actual_function(lb,'float lbColl_80005EBC('),
            _actual_function(dolphin_mtx,'void C_MTXIdentity('),
            _actual_function(dolphin_mtx,'void C_MTXCopy('),
            _actual_function(hsd_mtx_header,'static inline f32 fabsf_bitwise('),
            _actual_function(hsd_mtx,'static inline f32 HSD_CalcDeterminantMatrix3x4('),
            _actual_function(hsd_mtx,'void HSD_MtxInverse('),
            _actual_function(ps_math,'void melee_web_ps_mtx_mult_vec('),
        ))
        offline_plain=offline_helpers+'\n'+offline_inner+'\n'

        inverse_instrumented=_actual_function(hsd_mtx,'void HSD_MtxInverse(')
        inverse_instrumented=_replace_source_once(inverse_instrumented,
            'f32 det = HSD_CalcDeterminantMatrix3x4(src);',
            'f32 det = HSD_CalcDeterminantMatrix3x4(src);\n'
            '    MELEE_TRACE_VALUE("inverse_determinant", det);',
            'inverse determinant')
        inverse_instrumented=_replace_source_once(inverse_instrumented,
            'if (fabsf_bitwise(det) < EPSILON) {',
            'if (fabsf_bitwise(det) < EPSILON) {\n'
            '        MELEE_TRACE_EVENT("inverse_singular_fallback");',
            'inverse fallback branch')
        inverse_instrumented=_replace_source_once(inverse_instrumented,
            'det = 1.0f / det;',
            'det = 1.0f / det;\n'
            '    MELEE_TRACE_VALUE("inverse_reciprocal_determinant", det);',
            'inverse reciprocal determinant')
        inverse_close=inverse_instrumented.rfind('}')
        inverse_instrumented=(inverse_instrumented[:inverse_close]+
            '    MELEE_TRACE_MTX("inverse_result", dest);\n'+inverse_instrumented[inverse_close:])

        multvec_instrumented=_actual_function(ps_math,'void melee_web_ps_mtx_mult_vec(')
        multvec_instrumented=_replace_source_once(multvec_instrumented,
            'float even = fmaf(m[row][2], z, m[row][0] * x);',
            'float even = fmaf(m[row][2], z, m[row][0] * x);\n'
            '        MELEE_TRACE_VALUE("mult_vec_even", even);',
            'mult-vector even lane')
        multvec_instrumented=_replace_source_once(multvec_instrumented,
            'float odd = fmaf(m[row][3], 1.0f, m[row][1] * y);',
            'float odd = fmaf(m[row][3], 1.0f, m[row][1] * y);\n'
            '        MELEE_TRACE_VALUE("mult_vec_odd", odd);',
            'mult-vector odd lane')
        multvec_instrumented=_replace_source_once(multvec_instrumented,
            'result[row] = even + odd;',
            'result[row] = even + odd;\n'
            '        MELEE_TRACE_VALUE("mult_vec_result", result[row]);',
            'mult-vector result')
        offline_instrumented='\n'.join((
            _actual_function(lb_header,'static inline bool approximatelyZero('),
            _actual_function(lb,'float lbColl_80005EBC('),
            _actual_function(dolphin_mtx,'void C_MTXIdentity('),
            _actual_function(dolphin_mtx,'void C_MTXCopy('),
            _actual_function(hsd_mtx_header,'static inline f32 fabsf_bitwise('),
            _actual_function(hsd_mtx,'static inline f32 HSD_CalcDeterminantMatrix3x4('),
            inverse_instrumented,
            multvec_instrumented,
            _instrument_inner_geometry(offline_inner),
        ))+'\n'
        cls.offline_plain_dir=cls.scratch/'offline-unchanged'
        cls.offline_plain_dir.mkdir()
        (cls.offline_plain_dir/'offline_source_bodies.inc').write_text(
            epsilon_definition+offline_plain)
        cls.offline_instrumented_dir=cls.scratch/'offline-instrumented'
        cls.offline_instrumented_dir.mkdir()
        (cls.offline_instrumented_dir/'offline_source_bodies.inc').write_text(
            epsilon_definition+offline_instrumented)
        cls.candidate_binary=cls.scratch/'actual-candidates'
        compiler=shutil.which('cc') or 'cc'
        common=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-ffp-contract=off']
        fixture=str(ROOT/'tests/native_hit_candidate_probe_fixture.c')
        identity=str(cls.scratch/'identity.c')
        probe=str(ROOT/'src/gameplay_hit_transition_probe.c')
        def compile_fixture(binary,defines,sources,log_name,include_dirs=None):
            include_dirs=include_dirs or (cls.scratch,)
            include_args=['-I'+str(directory) for directory in include_dirs]
            result=subprocess.run(common+include_args+['-I'+str(ROOT/'src')]+defines+
                sources+['-lm','-o',str(binary)],capture_output=True,text=True)
            log=result.stdout+result.stderr
            (cls.scratch/log_name).write_text(log)
            _retain_reducer_evidence(log_name,log,'')
            return result
        cls.offline_plain_binary=cls.scratch/'offline-inner-unchanged'
        built=compile_fixture(cls.offline_plain_binary,
            ['-DMELEE_WEB_HIT_PROBE_OFFLINE_REDUCER=1'],[fixture],
            'offline-inner-unchanged-compile.log',
            (cls.offline_plain_dir,cls.scratch))
        if built.returncode:raise _compile_failure(
            'Unchanged offline inner-helper compile failed',built,cls.scratch,
            'offline-inner-unchanged-compile.log')
        cls.offline_instrumented_binary=cls.scratch/'offline-inner-instrumented'
        built=compile_fixture(cls.offline_instrumented_binary,
            ['-DMELEE_WEB_HIT_PROBE_OFFLINE_REDUCER=1',
             '-DMELEE_WEB_HIT_PROBE_INSTRUMENTED=1'],[fixture],
            'offline-inner-instrumented-compile.log',
            (cls.offline_instrumented_dir,cls.scratch))
        if built.returncode:raise _compile_failure(
            'Instrumented offline inner-helper compile failed',built,cls.scratch,
            'offline-inner-instrumented-compile.log')
        built=compile_fixture(cls.candidate_binary,
            ['-DMELEE_WEB_RNG_DRAW_OBSERVER=1','-DMELEE_WEB_HIT_PROBE_SYNTHETIC=1'],
            [probe,fixture,identity],'candidate-compile.log')
        if built.returncode:raise _compile_failure(
            'Actual bodies/synthetic ABI compile failed',built,cls.scratch,'candidate-compile.log')
        cls.no_observer_binary=cls.scratch/'actual-candidates-no-observer'
        built=compile_fixture(cls.no_observer_binary,[],[probe,fixture,identity],'no-observer-compile.log')
        if built.returncode:raise _compile_failure(
            'No-observer comparison compile failed',built,cls.scratch,'no-observer-compile.log')
        cls.missing_inner_dir=cls.scratch/'missing-inner-hook'
        cls.missing_inner_dir.mkdir()
        (cls.missing_inner_dir/'candidate_source_bodies.inc').write_text(bodies.replace(hook_call,''))
        cls.missing_inner_binary=cls.scratch/'actual-candidates-missing-inner-hook'
        built=compile_fixture(cls.missing_inner_binary,
            ['-DMELEE_WEB_RNG_DRAW_OBSERVER=1','-DMELEE_WEB_HIT_PROBE_SYNTHETIC=1'],
            [probe,fixture,identity],'missing-inner-compile.log',
            (cls.missing_inner_dir,cls.scratch))
        if built.returncode:raise _compile_failure(
            'Missing-inner fail-closed variant compile failed',built,cls.scratch,'missing-inner-compile.log')
        cls.duplicate_inner_dir=cls.scratch/'duplicate-inner-hook'
        cls.duplicate_inner_dir.mkdir()
        (cls.duplicate_inner_dir/'candidate_source_bodies.inc').write_text(
            bodies.replace(hook_call,hook_call+hook_call))
        cls.duplicate_inner_binary=cls.scratch/'actual-candidates-duplicate-inner-hook'
        built=compile_fixture(cls.duplicate_inner_binary,
            ['-DMELEE_WEB_RNG_DRAW_OBSERVER=1','-DMELEE_WEB_HIT_PROBE_SYNTHETIC=1'],
            [probe,fixture,identity],'duplicate-inner-compile.log',
            (cls.duplicate_inner_dir,cls.scratch))
        if built.returncode:raise _compile_failure(
            'Duplicate-inner fail-closed variant compile failed',built,cls.scratch,'duplicate-inner-compile.log')
        cls.adapter_binary=cls.scratch/'fixture-counting-adapter'
        built=compile_fixture(cls.adapter_binary,
            ['-DMELEE_WEB_RNG_DRAW_OBSERVER=1','-DMELEE_WEB_HIT_PROBE_SYNTHETIC=1',
             '-DMELEE_WEB_HIT_PROBE_COUNTING_ADAPTER=1'],
            [fixture,identity],'counting-adapter-compile.log')
        if built.returncode:raise _compile_failure(
            'Fixture-only counting adapter compile failed',built,cls.scratch,'counting-adapter-compile.log')
    def run(self,result=None):
        result=unittest.TestCase.run(self,result)
        self.__class__.test_result=result
        return result
    @classmethod
    def tearDownClass(cls):HitTransitionProbeTest.tearDownClass.__func__(cls)
    def candidate_mode(self,mode,valid=True):
        result=subprocess.run([str(self.candidate_binary),mode],capture_output=True,text=True)
        (self.scratch/('candidate-'+mode+'.stdout')).write_text(result.stdout)
        (self.scratch/('candidate-'+mode+'.stderr')).write_text(result.stderr)
        if not valid:
            self.assertNotEqual(result.returncode,0,result.stderr)
            if mode=="overflow":
                partial=json.loads(result.stdout.splitlines()[-1])
                self.assertTrue(partial["overflowed"])
                self.assertEqual(len(partial["events"]),76)
            return
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('exact_state_writes=1',result.stderr)
        rows=[json.loads(x) for x in result.stdout.splitlines()]
        validated=subprocess.run(['node','--input-type=module','-e',
            "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
            "let t='';for await(const c of process.stdin)t+=c;"
            "validateHitTransitionProbeRows(JSON.parse(t).map(JSON.stringify),parseHitTransitionProbe('5238,5239,5240'),{observedCursor:5240});"],
            cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
        (self.scratch/('candidate-'+mode+'.validation')).write_text(validated.stdout+validated.stderr)
        self.assertEqual(validated.returncode,0,validated.stderr)
        return rows[1]
    def run_reducer_binary(self,binary,mode,label):
        result=subprocess.run([str(binary),mode],capture_output=True,text=True)
        _retain_reducer_evidence(label,result.stdout,result.stderr)
        (self.scratch/(label+'.stdout')).write_text(result.stdout)
        (self.scratch/(label+'.stderr')).write_text(result.stderr)
        return result
    def test_offline_captured_inner_geometry(self):
        capture=os.environ.get('MELEE_HIT_INNER_GEOMETRY_CAPTURE')
        if not capture:self.skipTest('set MELEE_HIT_INNER_GEOMETRY_CAPTURE to a retained cursor-5239 capture')
        records=_capture_inner_geometry(capture)
        capture_path=Path(capture)
        input_path=self.scratch/'inner-geometry-input.tsv'
        with input_path.open('w') as output:
            for hit,hurt,result,bits in records:
                output.write(' '.join([str(5239),str(hit),str(hurt),str(result),*bits])+'\n')
        evidence=os.environ.get('MELEE_HIT_REDUCER_EVIDENCE_DIR')
        evidence_dir=Path(evidence) if evidence else None
        if evidence_dir:
            evidence_dir.mkdir(parents=True,exist_ok=True)
            shutil.copy2(input_path,evidence_dir/'inner-geometry-input.tsv')
            shutil.copy2(capture_path,evidence_dir/'capture-source.jsonl')
            shutil.copy2(self.offline_plain_dir/'offline_source_bodies.inc',
                evidence_dir/'offline-unchanged-source.inc')
            shutil.copy2(self.offline_instrumented_dir/'offline_source_bodies.inc',
                evidence_dir/'offline-instrumented-source.inc')
            shutil.copy2(self.offline_plain_binary,evidence_dir/'offline-inner-unchanged')
            shutil.copy2(self.offline_instrumented_binary,evidence_dir/'offline-inner-instrumented')
        plain=subprocess.run([str(self.offline_plain_binary),str(input_path)],
            capture_output=True,text=True)
        instrumented=subprocess.run([str(self.offline_instrumented_binary),str(input_path)],
            capture_output=True,text=True)
        for label,result in (('unchanged',plain),('instrumented',instrumented)):
            (self.scratch/f'inner-geometry-{label}.stdout').write_text(result.stdout)
            (self.scratch/f'inner-geometry-{label}.stderr').write_text(result.stderr)
            if evidence_dir:
                (evidence_dir/f'inner-geometry-{label}.stdout').write_text(result.stdout)
                (evidence_dir/f'inner-geometry-{label}.stderr').write_text(result.stderr)
        self.assertEqual(plain.returncode,0,plain.stderr)
        self.assertEqual(instrumented.returncode,0,instrumented.stderr)
        plain_rows=[json.loads(line) for line in plain.stdout.splitlines()]
        instrumented_rows=[json.loads(line) for line in instrumented.stdout.splitlines()]
        self.assertEqual(len(plain_rows),60)
        self.assertEqual(len(instrumented_rows),60)
        expected_pairs=[(hit,hurt) for hit in range(4) for hurt in range(15)]
        self.assertEqual([(row['hit_index'],row['hurt_index']) for row in plain_rows],expected_pairs)
        self.assertEqual(plain_rows,instrumented_rows,
            'instrumentation changed the source result or written outputs')
        self.assertEqual([row['captured_result'] for row in plain_rows],[0]*60)
        self.assertEqual([row['result'] for row in plain_rows],[row['captured_result'] for row in plain_rows],
            'extracted source result differs from captured inner result')

        sentinel='c640e400'
        captured_events=_captured_geometry_by_key(
            map(json.loads,capture_path.read_text().splitlines()))
        expected_keys={(row['cursor'],row['hit_index'],row['hurt_index']) for row in instrumented_rows}
        scalar_labels=set(('broadphase_radius hit_len_sq hurt_len_sq segment_dot '
            'hit_start_dot hurt_start_dot closest_denom hit_param hurt_param closest_dist_sq '
            'axis_distance inverse_determinant inverse_reciprocal_determinant local_distance '
            'scaled_hurt_radius contact_lerp allowed_distance overlap mult_vec_even '
            'mult_vec_odd mult_vec_result').split())
        bound_labels={axis+'_'+suffix for axis in 'xyz' for suffix in
            ('upper_from_start','lower_from_end','lower_from_start','upper_from_end')}
        vector_labels={'hit_closest','hurt_closest','hit_closest_local','hurt_closest_local','contact'}
        matrix_labels={'inverse_result','inverse_hurt_matrix'}
        event_labels={'solver_zero_hurt_length','solver_nonzero_hurt_length',
            'solver_parallel_axes','solver_nonparallel_axes','inverse_singular_fallback',
            'distance_reject','distance_accept'}
        widths={label:1 for label in scalar_labels|bound_labels|
            {f'aabb_reject_{i:02d}' for i in range(1,13)}}
        widths.update({label:3 for label in vector_labels})
        widths.update({label:12 for label in matrix_labels})
        widths.update({label:0 for label in event_labels})
        trace_rows={}
        for line in instrumented.stderr.splitlines():
            fields=line.split()
            if not fields or fields[0]!='TRACE':continue
            _,cursor,hit,hurt,sequence,label,*values=fields
            key=(int(cursor),int(hit),int(hurt))
            self.assertIn(key,expected_keys,'trace refers to an unknown geometry record')
            self.assertIn(label,widths,'unknown source trace label')
            self.assertEqual(len(values),widths[label],f'trace operand width for {label}')
            events=trace_rows.setdefault(key,[])
            self.assertEqual(int(sequence),len(events),f'noncontiguous trace sequence for {key}')
            for value in values:
                self.assertRegex(value,r'^[0-9a-f]{8}$')
                self.assertTrue(math.isfinite(struct.unpack('>f',bytes.fromhex(value))[0]),
                    f'nonfinite trace operand for {key}: {label}')
            events.append((label,values))
        self.assertEqual(set(trace_rows),expected_keys)
        min_gap=None;adjacent=0;fallbacks=0;aabb_rejects=0
        for row in instrumented_rows:
            key=(row['cursor'],row['hit_index'],row['hurt_index'])
            events=trace_rows.get(key,[])
            labels=[label for label,_ in events]
            self.assertTrue(events,f'missing instrumented intermediate trace for {key}')
            # Only the vector helper lane snapshots repeat: three rows for
            # each of the two calls. All other labels denote one source site.
            lane_labels={'mult_vec_even','mult_vec_odd','mult_vec_result'}
            for label in set(labels)-lane_labels:
                self.assertEqual(labels.count(label),1,f'duplicate trace label for {key}: {label}')
            reject_labels=[label for label in labels if label.startswith('aabb_reject_') or label=='distance_reject']
            self.assertEqual(len(reject_labels),1,f'expected exactly one reject tag for {key}')
            self.assertIn('broadphase_radius',labels)
            if 'distance_reject' in labels:
                required={'hit_len_sq','hurt_len_sq','segment_dot','hit_start_dot',
                    'hurt_start_dot','closest_denom','hit_param','hurt_param',
                    'closest_dist_sq','axis_distance','hit_closest','hurt_closest',
                    'inverse_determinant','inverse_reciprocal_determinant','inverse_result',
                    'inverse_hurt_matrix','hit_closest_local','hurt_closest_local',
                    'local_distance','scaled_hurt_radius','contact_lerp','allowed_distance',
                    'overlap','contact','solver_nonzero_hurt_length','solver_parallel_axes'}
                self.assertTrue(required.issubset(labels),f'missing solver trace labels for {key}: {required-set(labels)}')
                for label in lane_labels:self.assertEqual(labels.count(label),6)
            else:
                self.assertRegex(reject_labels[0],r'^aabb_reject_(0[1-9]|1[0-2])$')
                self.assertFalse(set(labels)&lane_labels)
                reject_index=int(reject_labels[0].split('_')[-1])-1
                reject_axis=reject_index//4
                endpoint_bits=captured_events[key]['inner_endpoints_bits']
                expected_bounds=[]
                for axis in range(reject_axis+1):
                    start=struct.unpack('>f',bytes.fromhex(endpoint_bits[axis]))[0]
                    end=struct.unpack('>f',bytes.fromhex(endpoint_bits[3+axis]))[0]
                    suffixes=('upper_from_start','lower_from_end') if start>end else ('lower_from_start','upper_from_end')
                    count=2 if axis<reject_axis else reject_index%2+1
                    expected_bounds.extend('xyz'[axis]+'_'+s for s in suffixes[:count])
                self.assertEqual([label for label in labels if label in bound_labels],expected_bounds,
                    f'missing or misplaced AABB operand snapshots for {key}')
            aabb_rejects+=sum(label.startswith('aabb_reject_') for label in labels)
            fallbacks+=labels.count('inverse_singular_fallback')
            if row['result']==0 and 'distance_reject' in labels:
                values={label:items[0] for label,items in events if items}
                axis_bits=int(values['axis_distance'],16)
                allowed_bits=int(values['allowed_distance'],16)
                self.assertLess(allowed_bits,axis_bits,
                    f'false result did not take the captured distance-reject branch: {key}')
                gap=axis_bits-allowed_bits
                min_gap=gap if min_gap is None else min(min_gap,gap)
                adjacent+=int(allowed_bits+1==axis_bits)
            for field in ('hit_closest','hurt_closest','contact'):
                self.assertEqual(len(row[field]),3)
            if 'distance_reject' in labels:
                self.assertNotEqual(row['overlap'],sentinel)
                self.assertTrue(all(bit!=sentinel for bit in row['contact']))
                self.assertEqual(row['contact']+[row['overlap']],
                    captured_events[key]['geometry_after'][6:10],
                    f'solver written outputs differ from capture for {key}')
            else:
                self.assertTrue(any(label.startswith('aabb_reject_') for label in labels),
                    f'false result had no observed reject branch: {key}')
                self.assertEqual(row['overlap'],sentinel)
                self.assertTrue(all(bit==sentinel for bit in row['contact']))

        if evidence_dir:
            summary={
                'capture_sha256':__import__('hashlib').sha256(capture_path.read_bytes()).hexdigest(),
                'capture_source_cursor':5239,
                'geometry_records':len(records),
                'captured_results':{'false':60},
                'source_result_matches_captured':True,
                'instrumented_result_and_output_bits_match_unchanged':True,
                'distance_reject_records':sum(
                    any(label=='distance_reject' for label,_ in trace_rows.get((5239,row['hit_index'],row['hurt_index']),[]))
                    for row in instrumented_rows),
                'aabb_reject_count':aabb_rejects,
                'singular_matrix_fallback_count':fallbacks,
                'minimum_positive_float_word_gap_axis_minus_allowed':min_gap,
                'adjacent_positive_float_pairs':adjacent,
                'diagnostic_limit':'host float sensitivity only; not PPC equivalence or causal attribution',
                'matrix_closure':'extracted HSD_MtxInverse and determinant; matrix input is row-major captured bits',
                'vector_closure':'extracted melee_web_ps_mtx_mult_vec with explicit fmaf; -ffp-contract=off',
            }
            (evidence_dir/'inner-geometry-reducer-summary.json').write_text(
                json.dumps(summary,indent=2)+'\n')
    def test_authored_selector_off_completes_all_sixty_geometry_calls(self):
        observed=self.run_reducer_binary(self.candidate_binary,'authored_off','selector-off-observer')
        uninstrumented=self.run_reducer_binary(self.no_observer_binary,'authored_off','selector-off-no-observer')
        for result in (observed,uninstrumented):
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(result.stdout,'')
            self.assertIn('geometry_calls=60 emitted_rows=0',result.stderr)
        observed_hash=re.search(r'state_hash=([0-9a-f]{16})',observed.stderr)
        uninstrumented_hash=re.search(r'state_hash=([0-9a-f]{16})',uninstrumented.stderr)
        self.assertIsNotNone(observed_hash,observed.stderr)
        self.assertIsNotNone(uninstrumented_hash,uninstrumented.stderr)
        self.assertEqual(observed_hash.group(1),uninstrumented_hash.group(1))
    def test_authored_four_by_fifteen_completes_and_validates_all_76_rows(self):
        result=self.run_reducer_binary(self.candidate_binary,'authored_complete','actual-helper-complete-76')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(len(result.stdout.splitlines()),3)
        rows=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([row['source_cursor'] for row in rows],[5238,5239,5240])
        for legacy in (rows[0],rows[2]):
            self.assertNotIn('candidate_enabled',legacy)
            self.assertEqual(len(legacy['events']),2)
        row=rows[1]
        self.assertEqual(row['source_cursor'],5239)
        self.assertFalse(row['overflowed'])
        self.assertEqual(row['candidate_enabled'],True)
        self.assertEqual((row['candidate_passes'],row['candidate_pairs']),(1,1))
        self.assertEqual(row['hook_counts'],[1,2,0])
        events=row['events']
        self.assertEqual(len(events),76)
        self.assertEqual([event['sequence'] for event in events],list(range(76)))
        expected_order=['scheduler_start','entry','pass_entry','pair_entry']
        for hit in range(4):
            expected_order.append('candidate')
            expected_order.extend(['geometry']*15)
        expected_order.extend(['pair_return','pass_return','entry','return','entry','return','return','scheduler_return'])
        self.assertEqual([event['phase'] for event in events],expected_order)
        self.assertEqual([event['hit_index'] for event in events if event['phase']=='candidate'],[0,1,2,3])
        for event in (event for event in events if event['phase']=='candidate'):
            self.assertEqual(len(event['hit_flags']),6)
            self.assertEqual(re.fullmatch(r'[0-9a-f]{8}',event['hit_damage_bits']).group(0),event['hit_damage_bits'])
            self.assertEqual(re.fullmatch(r'[0-9a-f]{8}',event['hit_radius_bits']).group(0),event['hit_radius_bits'])
        geometries=[event for event in events if event['phase']=='geometry']
        self.assertEqual(len(geometries),60)
        selected_line=next(line for line in result.stdout.splitlines() if '"source_cursor":5239' in line)
        serialized_sizes=_serialized_event_sizes(selected_line)
        max_serialized_bytes=max(serialized_sizes)
        _retain_reducer_evidence('actual-helper-complete-76-max-row-bytes',
            f'max_serialized_event_bytes={max_serialized_bytes}\nrow_capacity_bytes=2048\n', '')
        self.assertEqual(len(serialized_sizes),76)
        self.assertLess(max_serialized_bytes,2048)
        self.assertEqual([(event['hit_index'],event['hurt_index']) for event in geometries],
            [(hit,hurt) for hit in range(4) for hurt in range(15)])
        self.assertTrue(all(event['result']==0 for event in geometries))
        for event in geometries:
            self.assertEqual(len(event['geometry_before']),18)
            self.assertEqual(len(event['geometry_after']),10)
            self.assertEqual(len(event['arguments']),3)
            self.assertEqual(len(event['matrix_bits']),12)
            for field in ('geometry_before','geometry_after','arguments','matrix_bits'):
                self.assertTrue(all(re.fullmatch(r'[0-9a-f]{8}',value) for value in event[field]))
            self.assertEqual(event['arguments'],['3f800000','3f800000','80000000'])
            self.assertEqual(event['matrix_bits'],['00000000']*12)
            hurt=event['hurt_index']
            self.assertEqual(event['hurt_bone_idx'],hurt)
            self.assertEqual(event['geometry_after'],[
                struct.pack('>f',float(100+hurt)).hex(),
                '00000000','00000000',
                struct.pack('>f',float(101+hurt)).hex(),
                '00000000','00000000','00000000','00000000','00000000','00000000'])
        self.assertEqual((geometries[-1]['hit_index'],geometries[-1]['hurt_index']),(3,14))
        self.assertRegex(result.stderr,r'geometry_calls=60 rows=76 enabled_state_hash=([0-9a-f]{16}) selector_off_state_hash=\1')
        validation=subprocess.run(['node','--input-type=module','-e',
            "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
            "let t='';for await(const c of process.stdin)t+=c;const rows=JSON.parse(t);"
            "const selection=parseHitTransitionProbe('5238,5239,5240');"
            "const result=validateHitTransitionProbeRows(rows.map(JSON.stringify),selection,{observedCursor:5240});"
            "if(!result.complete)throw Error('76-row actual candidate artifact incomplete');"],
            cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
        _retain_reducer_evidence('actual-helper-complete-76-validation',validation.stdout,validation.stderr)
        self.assertEqual(validation.returncode,0,validation.stderr)
        header_checks=subprocess.run(['node','--input-type=module','-e',
            "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
            "let t='';for await(const c of process.stdin)t+=c;const rows=JSON.parse(t);"
            "const selection=parseHitTransitionProbe('5238,5239,5240');"
            "validateHitTransitionProbeRows(rows.map(JSON.stringify),selection,{observedCursor:5240});"
            "const variants=[];const missing=structuredClone(rows);delete missing[1].candidate_enabled;"
            "delete missing[1].candidate_passes;delete missing[1].candidate_pairs;variants.push(missing);"
            "const bad=structuredClone(rows);bad[1].candidate_passes='1';variants.push(bad);"
            "const legacy=structuredClone(rows);legacy[1].source_cursor=5240;variants.push(legacy);"
            "for(const value of variants){let rejected=false;try{validateHitTransitionProbeRows(value.map(JSON.stringify),selection,{observedCursor:5240});}catch{rejected=true;}"
            "if(!rejected)throw Error('76-row allowance accepted a missing or invalid candidate header');}"],
            cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
        _retain_reducer_evidence('candidate-header-bound-controls',header_checks.stdout,header_checks.stderr)
        self.assertEqual(header_checks.returncode,0,header_checks.stderr)

    def test_actual_inner_aabb_reject_observer_selector_controls(self):
        observed=self.run_reducer_binary(self.candidate_binary,'aabb_reject',
            'actual-inner-aabb-reject-observer')
        uninstrumented=self.run_reducer_binary(self.no_observer_binary,'aabb_reject',
            'actual-inner-aabb-reject-no-observer')
        self.assertEqual(observed.returncode,0,observed.stderr)
        self.assertEqual(uninstrumented.returncode,0,uninstrumented.stderr)
        self.assertEqual(uninstrumented.stdout,'')
        rows=[json.loads(line) for line in observed.stdout.splitlines()]
        self.assertEqual([row['source_cursor'] for row in rows],[5238,5239,5240])
        geometry=next(event for event in rows[1]['events'] if event['phase']=='geometry')
        self.assertEqual(geometry['result'],0)
        self.assertEqual(geometry['hurt_bone_idx'],0)
        self.assertEqual(geometry['inner_call_count'],1)
        self.assertTrue(geometry['inner_matrix_present'])
        self.assertEqual(geometry['inner_matrix_bits'],[
            '3f800000','00000000','00000000','00000000',
            '00000000','3f800000','00000000','00000000',
            '00000000','00000000','3f800000','00000000'])
        self.assertEqual(geometry['inner_effective_arguments_bits'],
            ['40000000','40400000','40400000'])
        control=re.compile(
            r'SOURCE_OBSERVATION_CONTROL result=0 log0=0 log1=0 calls=3 ordered=3 '
            r'geometry_calls=1 matrix_getters=1 matrix_concats=0 inverse_calls=0 vector_calls=0 '
            r'selector_off_state_hash=([0-9a-f]{16}) selected_state_hash=([0-9a-f]{16}) '
            r'exact_state_writes=1')
        observed_control=control.search(observed.stderr)
        plain_control=control.search(uninstrumented.stderr)
        self.assertIsNotNone(observed_control,observed.stderr)
        self.assertIsNotNone(plain_control,uninstrumented.stderr)
        self.assertEqual(observed_control.groups()[0],observed_control.groups()[1])
        self.assertEqual(plain_control.groups()[0],plain_control.groups()[1])
        self.assertEqual(observed_control.groups()[0],plain_control.groups()[0])
        call_order='SOURCE_OBSERVATION_CALL_ORDER 5,1,3'
        self.assertIn(call_order,observed.stderr)
        self.assertIn(call_order,uninstrumented.stderr)
    def test_actual_inner_remaining_observer_selector_controls(self):
        for mode in ('zero_distance','nonzero_distance','cache','intangible','mode','matrix'):
            with self.subTest(mode=mode):
                observed=self.run_reducer_binary(self.candidate_binary,mode,
                    'actual-inner-'+mode+'-observer')
                uninstrumented=self.run_reducer_binary(self.no_observer_binary,mode,
                    'actual-inner-'+mode+'-no-observer')
                for result in (observed,uninstrumented):
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertIn('exact_state_writes=1',result.stderr)
                    hashes=re.search(r'selector_off_state_hash=([0-9a-f]{16}) '
                        r'selected_state_hash=([0-9a-f]{16})',result.stderr)
                    self.assertIsNotNone(hashes,result.stderr)
                    self.assertEqual(hashes.group(1),hashes.group(2))
                self.assertEqual(uninstrumented.stdout,'')
                # The fixture asserts all Fighter/log writes, RNG and service
                # counts/order exactly; compare its complete diagnostics across binaries.
                self.assertEqual(observed.stderr,uninstrumented.stderr)
                rows=[json.loads(line) for line in observed.stdout.splitlines()]
                validated=subprocess.run(['node','--input-type=module','-e',
                    "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
                    "let t='';for await(const c of process.stdin)t+=c;"
                    "if(!validateHitTransitionProbeRows(JSON.parse(t).map(JSON.stringify),"
                    "parseHitTransitionProbe('5238,5239,5240'),{observedCursor:5240}).complete)"
                    "throw Error('incomplete actual inner control');"],
                    cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
                _retain_reducer_evidence('actual-inner-'+mode+'-validation',
                    validated.stdout,validated.stderr)
                self.assertEqual(validated.returncode,0,validated.stderr)

    def test_authored_candidate_row_77_overflows_at_76_and_retains_all_rows(self):
        result=self.run_reducer_binary(self.candidate_binary,'candidate_overflow','actual-helper-row77-overflow')
        self.assertNotEqual(result.returncode,0,result.stderr)
        self.assertIn('attempted_geometry_calls=60 retained_records=76',result.stderr)
        self.assertIn('diagnostic 76-row overflow',result.stderr)
        self.assertEqual(len(result.stdout.splitlines()),1)
        row=json.loads(result.stdout.splitlines()[0])
        self.assertEqual(row['source_cursor'],5239)
        self.assertTrue(row['overflowed'])
        self.assertEqual(row['hook_counts'],[2,2,0])
        events=row['events']
        self.assertEqual(len(events),76)
        self.assertEqual([event['sequence'] for event in events],list(range(76)))
        geometries=[event for event in events if event['phase']=='geometry']
        self.assertEqual(len(geometries),60)
        self.assertEqual([(event['hit_index'],event['hurt_index']) for event in geometries],
            [(hit,hurt) for hit in range(4) for hurt in range(15)])
        self.assertEqual(events[-1]['phase'],'entry')
        self.assertEqual((events[-1]['kind'],events[-1]['invocation']),(0,4))
    def test_candidate_header_missing_keeps_5239_at_legacy_64_rows(self):
        result=self.run_reducer_binary(self.candidate_binary,'candidate_header_missing_overflow',
            'candidate-header-missing-64-overflow')
        self.assertNotEqual(result.returncode,0,result.stderr)
        self.assertIn('attempted_geometry_calls=0 retained_records=64',result.stderr)
        self.assertIn('diagnostic 64-row overflow',result.stderr)
        row=json.loads(result.stdout.splitlines()[0])
        self.assertEqual(row['source_cursor'],5239)
        self.assertTrue(row['overflowed'])
        self.assertNotIn('candidate_enabled',row)
        self.assertEqual(len(row['events']),64)
        self.assertEqual([event['sequence'] for event in row['events']],list(range(64)))
        self.assertEqual((row['events'][-1]['phase'],row['events'][-1]['invocation']),('entry',32))
        self.assertEqual(row['hook_counts'],[32,0,0])
    def counting_adapter_mode(self,mode,label):
        result=self.run_reducer_binary(self.adapter_binary,mode,label)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('source_state_writes_equal=1 service_counts_and_order_equal=1',result.stderr)
        row=json.loads(result.stdout)
        self.assertIn('fixture-only counting adapter',row['adapter_scope'])
        self.assertEqual(row['event_count'],len(row['events']))
        self.assertEqual([event['sequence'] for event in row['events']],list(range(row['event_count'])))
        counts={}
        for event in row['events']:counts[event['phase']]=counts.get(event['phase'],0)+1
        self.assertEqual([(event['kind'],event['ordinal']) for event in row['events']
                          if event['phase']=='return'],[(1,2),(1,3),(0,1)])
        self.assert_common_projection(mode,row['events'])
        return counts,row['events']
    def assert_common_projection(self,mode,events):
        expected=[('scheduler_start',3,0),('entry',0,1),('pass_entry',4,1),('pair_entry',4,1)]
        geometry_ordinal=0;producer_ordinal=0
        for hit in range(4):
            expected.append(('candidate',4,1))
            for hurt in range(15 if mode=='count_all_false' else 1):
                geometry_ordinal+=1;expected.append(('geometry',4,geometry_ordinal))
                if mode!='count_all_false':
                    producer_ordinal+=1
                    expected.append(('producer_entry',4,producer_ordinal))
                    branch_count=3 if mode=='count_mixed' and hit==3 else 1
                    expected.extend([('producer_branch',4,producer_ordinal)]*branch_count)
                    expected.append(('producer_return',4,producer_ordinal))
        expected.extend([('pair_return',4,1),('pass_return',4,1)])
        for invocation in (2,3):
            expected.append(('entry',1,invocation))
            if mode=='count_mixed' and invocation==2:expected.append(('log',1,invocation))
            expected.append(('return',1,invocation))
        expected.extend([('return',0,1),('scheduler_return',3,0)])
        actual=[(event['sequence'],event['phase'],event['kind'],event['ordinal']) for event in events]
        self.assertEqual(actual,[(sequence,*projection) for sequence,projection in enumerate(expected)])
    def test_fixture_counting_adapter_all_false_records_all_sixty_geometry_calls(self):
        counts,events=self.counting_adapter_mode('count_all_false','adapter-all-false')
        self.assertEqual(len(events),76)
        self.assertEqual(counts,{'scheduler_start':1,'entry':3,'pass_entry':1,'pair_entry':1,
            'candidate':4,'geometry':60,'pair_return':1,'pass_return':1,'return':3,'scheduler_return':1})
        geometries=[event for event in events if event['phase']=='geometry']
        self.assertEqual([(event['hit_index'],event['hurt_index'],event['result']) for event in geometries],
            [(hit,hurt,0) for hit in range(4) for hurt in range(15)])
    def test_fixture_counting_adapter_preserves_phantom_and_final_normal_branch_order(self):
        counts,events=self.counting_adapter_mode('count_phantom','adapter-repeated-phantom-false')
        self.assertEqual(len(events),32)
        self.assertEqual(counts,{'scheduler_start':1,'entry':3,'pass_entry':1,'pair_entry':1,
            'candidate':4,'geometry':4,'producer_entry':4,'producer_branch':4,'producer_return':4,
            'pair_return':1,'pass_return':1,'return':3,'scheduler_return':1})
        phantom=[event for event in events if event['phase']=='producer_branch']
        self.assertEqual([(event['hit_index'],event['branch']) for event in phantom],[(i,0) for i in range(4)])
        returns=[event for event in events if event['phase']=='producer_return']
        self.assertEqual([(event['hit_index'],event['result']) for event in returns],[(i,0) for i in range(4)])
        counts,events=self.counting_adapter_mode('count_mixed','adapter-phantom-then-normal')
        self.assertEqual(len(events),35)
        self.assertEqual(counts,{'scheduler_start':1,'entry':3,'pass_entry':1,'pair_entry':1,
            'candidate':4,'geometry':4,'producer_entry':4,'producer_branch':6,'producer_return':4,
            'log':1,'pair_return':1,'pass_return':1,'return':3,'scheduler_return':1})
        branches=[event for event in events if event['phase']=='producer_branch']
        self.assertEqual([(event['hit_index'],event['branch']) for event in branches],
            [(0,0),(1,0),(2,0),(3,3),(3,4),(3,5)])
        returns=[event for event in events if event['phase']=='producer_return']
        self.assertEqual([(event['hit_index'],event['result']) for event in returns],
            [(0,0),(1,0),(2,0),(3,1)])
    def test_actual_candidate_branches_and_state_equivalence(self):
        for mode in ('normal','phantom','zero','wrong_pair','intangible','cache','mode','matrix','two_geometry','hurt15','zero_overlap','aabb_reject','zero_distance','nonzero_distance','hit_disabled','catch','flag_zero','air_miss','grab_blocked','eligibility_blocked','phantom_existing','phantom_busy','phantom_invulnerable','normal_invulnerable','normal_armored'):
            with self.subTest(mode=mode):self.candidate_mode(mode)

    def test_version2_records_actual_inner_call_and_explicit_outer_skips(self):
        for mode in ('aabb_reject','zero_distance','nonzero_distance','cache','matrix'):
            with self.subTest(mode=mode):
                row=self.candidate_mode(mode)
                geometry=next(event for event in row['events'] if event['phase']=='geometry')
                self.assertEqual(geometry['inner_call_count'],1)
                self.assertIsNone(geometry['inner_skip_reason'])
                self.assertEqual(len(geometry['inner_endpoints_bits']),12)
                self.assertEqual(len(geometry['inner_effective_arguments_bits']),3)
                self.assertTrue(geometry['inner_matrix_present'])
                self.assertEqual(len(geometry['inner_matrix_bits']),12)
                diagnostic=(self.scratch/('candidate-'+mode+'.stderr')).read_text()
                if mode in ('aabb_reject','zero_distance'):
                    self.assertIn('inverse_calls=0 vector_calls=0',diagnostic)
                if mode=='nonzero_distance':
                    self.assertIn('inverse_calls=1 vector_calls=2',diagnostic)
                if mode=='cache':self.assertTrue(geometry['cache_before'])
        for mode,reason in (('intangible','intangible'),('mode','mode_nonzero')):
            with self.subTest(skip=mode):
                row=self.candidate_mode(mode)
                geometry=next(event for event in row['events'] if event['phase']=='geometry')
                self.assertEqual(geometry['inner_call_count'],0)
                self.assertEqual(geometry['inner_skip_reason'],reason)
                self.assertIsNone(geometry['inner_endpoints_bits'])
                self.assertFalse(geometry['inner_matrix_present'])

    def test_version2_inner_fields_reject_corrupted_artifacts(self):
        self.candidate_mode('normal')
        rows=[json.loads(line) for line in
            (self.scratch/'candidate-normal.stdout').read_text().splitlines()]
        result=subprocess.run(['node','--input-type=module','-e',
            "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
            "let t='';for await(const c of process.stdin)t+=c;const base=JSON.parse(t);"
            "const selection=parseHitTransitionProbe('5238,5239,5240');"
            "const validate=r=>validateHitTransitionProbeRows(r.map(JSON.stringify),selection,{observedCursor:5240});"
            "if(!validate(base).complete)throw Error('valid v2 actual rows rejected');"
            "const changes=[r=>r.events.find(e=>e.phase==='geometry').inner_call_count=2,"
            "r=>r.events.find(e=>e.phase==='geometry').inner_endpoints_bits.pop(),"
            "r=>r.events.find(e=>e.phase==='geometry').hurt_bone_present=false,"
            "r=>r.events.find(e=>e.phase==='geometry').inner_matrix_present=false,"
            "r=>r.events.find(e=>e.phase==='geometry').hurt_bone_idx=2147483648];"
            "for(const change of changes){const r=structuredClone(base);const before=JSON.stringify(r);change(r[1]);"
            "if(JSON.stringify(r)===before)throw Error('corruption mutation did not change the actual row');let failed=false;"
            "try{validate(r);}catch{failed=true;}"
            "if(!failed)throw Error('corrupted v2 geometry artifact accepted');}"],
            cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
        _retain_reducer_evidence('v2-corrupted-artifact-validation',result.stdout,result.stderr)
        (self.scratch/'v2-corrupted-artifact-validation.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_actual_inner_hook_missing_or_duplicate_fails_closed(self):
        for binary,label in ((self.missing_inner_binary,'actual-helper-inner-hook-missing'),
                             (self.duplicate_inner_binary,'actual-helper-inner-hook-duplicate')):
            with self.subTest(label=label):
                result=self.run_reducer_binary(binary,'normal',label)
                self.assertEqual(result.returncode,-signal.SIGABRT,result.stderr)
                rows=[json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual([row['source_cursor'] for row in rows],[5238])
                self.assertFalse(rows[0]['overflowed'])
                self.assertEqual([event['phase'] for event in rows[0]['events']],
                    ['scheduler_start','scheduler_return'])

    def test_candidate_completion_and_authored_bounds_fail_closed(self):
        for mode in ('hurt16','gap','missing_pair','missing_pass','missing_geometry','duplicate_geometry','duplicate_pair','duplicate_pair_return','overflow'):
            with self.subTest(mode=mode):self.candidate_mode(mode,False)

    def test_actual_rows_reject_corrupted_candidate_artifacts(self):
        self.candidate_mode('normal')
        rows=[json.loads(line) for line in
            (self.scratch/'candidate-normal.stdout').read_text().splitlines()]
        # Accept the complete actual C output before corrupting one selected artifact field.
        result=subprocess.run(['node','--input-type=module','-e',
            "import {parseHitTransitionProbe,validateHitTransitionProbeRows} from './scripts/rng_draw_probe.mjs';"
            "let t='';for await(const c of process.stdin)t+=c;const base=JSON.parse(t);"
            "const selection=parseHitTransitionProbe('5238,5239,5240');"
            "const validate=r=>validateHitTransitionProbeRows(r.map(JSON.stringify),selection,{observedCursor:5240});"
            "if(!validate(base).complete)throw Error('valid actual candidate rows rejected');"
            "const changes=[r=>r.events.find(e=>e.phase==='pair_entry').hurt_length=16,"
            "r=>r.events.find(e=>e.phase==='geometry').geometry_before.pop(),"
            "r=>r.events.find(e=>e.phase==='producer_branch').branch=5,"
            "r=>r.events=r.events.filter(e=>e.phase!=='pair_return'),"
            "r=>r.candidate_pairs++];"
            "for(const change of changes){const r=structuredClone(base);const before=JSON.stringify(r);change(r[1]);"
            "if(JSON.stringify(r)===before)throw Error('corruption mutation did not change the actual row');let failed=false;"
            "try{validate(r);}catch{failed=true;}"
            "if(!failed)throw Error('corrupted artifact accepted');}"],
            cwd=ROOT,input=json.dumps(rows),capture_output=True,text=True)
        _retain_reducer_evidence('corrupted-artifact-validation',result.stdout,result.stderr)
        (self.scratch/'corrupted-artifact-validation.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stderr)

if __name__=="__main__":unittest.main()
