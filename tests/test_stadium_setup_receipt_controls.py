"""Asset-free component checks for the Stadium Setup receipt owner boundary.

This compiles the exact StageLast receipt bodies and original Stadium constructor
callback against explicit synthetic source-memory services. It does not prove
SDK allocation ABI, full Stage dispatch, live game behavior, or session accuracy.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path(os.environ.get("MELEE_WEB_CANDIDATE_SOURCE_ROOT", ROOT)).resolve()


def _function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing production function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body for production function: {signature}")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        nxt = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and nxt == "*":
                state = "comment"
                index += 1
            elif char == "/" and nxt == "/":
                state = "line-comment"
                index += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[start:index + 1]
        elif state in ("string", "char"):
            if char == "\\":
                index += 1
            elif ((state == "string" and char == '"')
                  or (state == "char" and char == "'")):
                state = "code"
        elif state == "comment" and char == "*" and nxt == "/":
            state = "code"
            index += 1
        elif state == "line-comment" and char == "\n":
            state = "code"
        index += 1
    raise AssertionError(f"unterminated production function: {signature}")


PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#define St_Kind_PStadium 3
#define MELEE_WEB_SOURCE_MEMORY_READ_OK 0
#define MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT 3
#define MELEE_WEB_STADIUM_SETUP_SNAPSHOT_VERSION 1U
typedef int32_t s32;
typedef struct HSD_GObj HSD_GObj;
typedef HSD_GObj Ground_GObj;
typedef void (*HSD_GObjEvent)(Ground_GObj*);
typedef struct HSD_JObj { int unused; } HSD_JObj;
typedef struct HSD_MObj { int unused; } HSD_MObj;
typedef struct { int source_heap_handle; uint64_t world_generation;
                 uint64_t allocation_generation_watermark; } MeleeWebSourceMemoryContext;
typedef struct { int source_heap_handle; uint32_t requested_bytes;
                 uint64_t world_generation, allocation_generation;
                 uint8_t live, reserved[7]; } MeleeWebSourceMemoryAllocation;
typedef struct { uint64_t generation; uint64_t ticks; } MeleeWebGameplayStats;
typedef struct { uint64_t sequence; } MeleeWebStadiumGoAlignmentEvent;
typedef struct { uint64_t generation, armed_world_ticks; uint32_t armed, failure;
                 MeleeWebStadiumGoAlignmentEvent stage_before, stage_after,
                     go_after, hud_after; } MeleeWebStadiumGoAlignmentSnapshot;
typedef struct { int captured; HSD_GObj *map0_ground, *map1_ground,
                 *map2_ground, *map5_ground; } MeleeWebStadiumMap2BufferOwner;
typedef struct { int diagnostic_only, stage_kind, ground_kind; } Definition;
typedef struct { int32_t x0, x4; } YakumonoParam;
typedef struct { int32_t xD8; void *xCC; int xC4_b0, xC4_b1; void *xD0;
                 float xD4; int xDC; int16_t xDE, xE0, xE2; int xE4;
                 void *xE8; } StadiumFields;
typedef struct { struct { unsigned b012; } x11_flags; int map_id;
                 struct { StadiumFields stadium; } u; HSD_GObj *gobj; } Ground;
struct HSD_GObj { void *user_data; };
typedef struct { HSD_GObj *map_gobjs[6]; void *yakumono_param; int grkind; } StageInfo;
typedef struct {
 uint32_t version; uint64_t world_generation, world_ticks; uint32_t source_frame;
 int32_t map_id, initial_countdown, current_countdown, parameter_x0, parameter_x4;
 uint32_t constructor_randi_call_count, constructor_random_bound_valid;
 int32_t constructor_random_bound; uint32_t constructor_random_result_valid;
 int32_t constructor_random_result; uint32_t source_seed_before, source_seed_after;
 int32_t ground_source_heap_handle; uint32_t ground_requested_bytes;
 uint64_t ground_allocation_generation; int32_t object_source_heap_handle;
 uint32_t object_requested_bytes; uint64_t object_allocation_generation;
 uint32_t constructor_callback_count, source_parameter_roots_agree;
} MeleeWebStadiumSetupSnapshot;
typedef struct MeleeWebStageLast {
 uint64_t generation; Definition *definition; int source_ordered;
 const void *source_yakumono; int stadium_started, stadium_ready_route, stadium_ready_armed;
 void *stadium_ready_object, *stadium_ready_proc;
 MeleeWebStadiumMap2BufferOwner stadium_map2_buffer_owner;
 MeleeWebStadiumGoAlignmentSnapshot stadium_go_alignment;
 struct { void *object, *proc; } stadium_manager;
 MeleeWebSourceMemoryAllocation stadium_manager_lease, stadium_manager_proc_lease;
 struct {
  unsigned seen, finished, failure, call_observed, bound_valid, result_valid;
  unsigned seed_before_valid, seed_after_valid;
  HSD_GObj *object; Ground *ground;
  MeleeWebSourceMemoryAllocation object_lease, ground_lease;
  int32_t parameter_x0, parameter_x4, initial_countdown;
  int32_t random_bound, random_result; uint32_t seed_before, seed_after;
 } stadium_setup;
} MeleeWebStageLast;

static MeleeWebStageLast *active;
static StageInfo stage_info;
static HSD_GObj *map2_object;
static MeleeWebGameplayStats gameplay_stats;
static MeleeWebSourceMemoryContext source_context;
static MeleeWebSourceMemoryAllocation object_lease, ground_lease;
static YakumonoParam parameter, foreign_parameter;
static YakumonoParam *yakumono_param, *cached_yakumono;
static uint32_t rng_seed;
static int rng_calls, rng_last_bound, rng_result, mutate_lease_on_map_init;
static unsigned buffer_journal_calls;
static unsigned char archive_scratch[0x50000];
static void *HSD_GObj_804D781C, *HSD_GObj_804D7838, *HSD_GObj_804D7830;
static void *HSD_GObj_804D7814, *HSD_GObj_804D7818;

static MeleeWebGameplayStats melee_web_gameplay_stats(void) { return gameplay_stats; }
static int melee_web_gameplay_world_exists(void) { return 1; }
static int melee_web_source_memory_healthy(void) { return 1; }
static int melee_web_source_memory_context_read(MeleeWebSourceMemoryContext *out)
{ if (!out) return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT; *out=source_context; return 0; }
static int melee_web_source_memory_allocation_read(const void *p, MeleeWebSourceMemoryAllocation *out)
{
 if (!p || !out) return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
 if (p == map2_object) { *out=object_lease; return 0; }
 if (map2_object && p == map2_object->user_data) { *out=ground_lease; return 0; }
 return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
}
static int melee_web_stage_selection_preflight(int kind) { return kind == St_Kind_PStadium; }
static uint32_t melee_web_match_source_frames(void) { return 0; }
static int melee_web_match_rng_seed_owner_read(uint32_t *out)
{ if (!out) return 0; *out=rng_seed; return 1; }
static int melee_web_grpstadium_cached_yakumono_matches(const void *p)
{ return p == cached_yakumono; }
static int melee_web_grpstadium_cached_yakumono_snapshot(const void *p, int32_t *x0, int32_t *x4)
{ if (p != cached_yakumono || !x0 || !x4) return 0; *x0=cached_yakumono->x0; *x4=cached_yakumono->x4; return 1; }
static HSD_GObj *Ground_GetMapGObj(int map_id) { return map_id == 2 ? map2_object : NULL; }
static int fail(char *e, size_t n, const char *m)
{ if (e && n) snprintf(e,n,"%s",m); return 0; }
static int ok(char *e, size_t n) { if (e && n) *e=0; return 1; }
static void Ground_801C2ED0(HSD_JObj *jobj, int map_id)
{
 (void)jobj; assert(map_id == 2);
 if (mutate_lease_on_map_init == 1) ++object_lease.allocation_generation;
 if (mutate_lease_on_map_init == 2) ++ground_lease.allocation_generation;
}
static void grAnime_801C8138(Ground_GObj *g, int id, int flags) { (void)g; assert(id==2); (void)flags; }
static HSD_MObj *lbDvd_GetPreloadedArchive(int id) { assert(id==0x7D5); return NULL; }
static void *HSD_MemAlloc(size_t n) { assert(n==sizeof(archive_scratch)); return archive_scratch; }
/* Explicit synthetic service for the retained patched callback journal.
 * This checks invocation identity only, not the real buffer-owner implementation. */
static void stadium_owner_journal_map2_buffer(
    Ground_GObj *g, void *lookup, void *stored)
{
 assert(g==map2_object && lookup==NULL && stored==archive_scratch);
 ++buffer_journal_calls;
}
static int grStadium_801D10F8(int n) { return n+1; }
static void mpLib_800575B0(int n) { (void)n; }
static void fn_801D13C8(Ground_GObj *g) { (void)g; }
static void Ground_801C10B8(Ground_GObj *g, HSD_GObjEvent cb) { (void)g; assert(cb==fn_801D13C8); }
static int HSD_Randi(int bound)
{ assert(bound>0); ++rng_calls; rng_last_bound=bound; rng_seed+=2; assert(rng_result>=0 && rng_result<bound); return rng_result; }
static HSD_JObj *GET_JOBJ(Ground_GObj *g) { (void)g; static HSD_JObj jobj; return &jobj; }
#define GET_GROUND(g) ((Ground*)((g)->user_data))
'''

TEST_MAIN = r'''
static void fixture_setup(int32_t x0, int32_t x4)
{
 static MeleeWebStageLast owner; static Definition definition;
 static Ground ground; static HSD_GObj object;
 memset(&stage_info,0,sizeof(stage_info)); memset(&parameter,0,sizeof(parameter));
 memset(&foreign_parameter,0,sizeof(foreign_parameter));
 memset(&owner,0,sizeof(owner)); memset(&ground,0,sizeof(ground)); memset(&object,0,sizeof(object));
 definition=(Definition){.diagnostic_only=1,.stage_kind=St_Kind_PStadium,.ground_kind=17};
 parameter=(YakumonoParam){x0,x4}; foreign_parameter=(YakumonoParam){x0,x4};
 yakumono_param=&parameter; cached_yakumono=&parameter; stage_info.yakumono_param=&parameter;
 stage_info.grkind=definition.ground_kind; stage_info.map_gobjs[2]=&object;
 object.user_data=&ground; ground.gobj=&object; ground.map_id=2; map2_object=&object;
 active=&owner; owner.generation=19; owner.definition=&definition; owner.source_ordered=1;
 owner.source_yakumono=&parameter; gameplay_stats=(MeleeWebGameplayStats){.generation=19,.ticks=0};
 source_context=(MeleeWebSourceMemoryContext){.source_heap_handle=7,.world_generation=19,.allocation_generation_watermark=99};
 object_lease=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.requested_bytes=sizeof(object),.world_generation=19,.allocation_generation=11,.live=1};
 ground_lease=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.requested_bytes=sizeof(ground),.world_generation=19,.allocation_generation=12,.live=1};
 rng_seed=17; rng_calls=0; rng_last_bound=0; rng_result=2; mutate_lease_on_map_init=0;
 buffer_journal_calls=0;
 HSD_GObj_804D781C=HSD_GObj_804D7838=HSD_GObj_804D7830=NULL;
 HSD_GObj_804D7814=HSD_GObj_804D7818=NULL;
}
static void complete_setup_phase(void)
{
 active->stadium_started=1; active->stadium_ready_route=1; active->stadium_ready_armed=1;
 active->stadium_map2_buffer_owner.captured=1;
 active->stadium_map2_buffer_owner.map2_ground=map2_object;
}
static void capture_setup(void)
{
 grStadium_801D13E0(map2_object);
 assert(buffer_journal_calls==1);
 complete_setup_phase();
}
static void assert_snapshot_refuses_unchanged(const char *message_part)
{
 MeleeWebStadiumSetupSnapshot out; unsigned char before[sizeof(out)]; char error[256]={0};
 memset(&out,0xA5,sizeof(out)); memcpy(before,&out,sizeof(out));
 assert(!melee_web_stage_last_stadium_setup_snapshot(active,&parameter,&out,error,sizeof(error)));
 assert(memcmp(&out,before,sizeof(out))==0);
 if(message_part) assert(strstr(error,message_part)!=NULL);
}
static void check_randi_branch(int32_t x0, int32_t x4, int expected_count)
{
 fixture_setup(x0,x4); capture_setup();
 assert(rng_calls==expected_count);
 MeleeWebStadiumSetupSnapshot out={0}; char error[256]={0};
 assert(melee_web_stage_last_stadium_setup_snapshot(active,&parameter,&out,error,sizeof(error)));
 assert(out.world_generation==19 && out.world_ticks==0 && out.source_frame==0);
 assert(out.constructor_callback_count==1 && out.initial_countdown==out.current_countdown);
 assert(out.parameter_x0==x0 && out.parameter_x4==x4 && out.source_parameter_roots_agree==1);
 if (expected_count) {
  int bound=(x0>x4)?x0-x4:x4-x0;
  assert(rng_calls==1 && rng_last_bound==bound && out.constructor_randi_call_count==1);
  assert(out.constructor_random_bound_valid==1 && out.constructor_random_bound==bound);
  assert(out.constructor_random_result_valid==1 && out.constructor_random_result==2);
  assert(out.source_seed_before==17 && out.source_seed_after==19);
  assert(out.current_countdown==(x0<x4 ? x0+2 : x4+2));
 } else {
  assert(x0==x4 && rng_calls==0 && out.constructor_randi_call_count==0);
  assert(out.constructor_random_bound_valid==0 && out.constructor_random_bound==0);
  assert(out.constructor_random_result_valid==0 && out.constructor_random_result==0);
  assert(out.source_seed_before==17 && out.source_seed_after==17);
  assert(out.current_countdown==x0);
 }
}
int main(void)
{
 check_randi_branch(3,7,1); /* original x4 > x0 branch */
 check_randi_branch(7,3,1); /* original x4 < x0 branch */
 check_randi_branch(5,5,0); /* equal endpoints: original no-call branch */

 fixture_setup(3,7); complete_setup_phase();
 assert_snapshot_refuses_unchanged("constructor receipt is absent");

 fixture_setup(3,7); assert(melee_web_stage_last_stadium_setup_constructor_begin(map2_object));
 assert(!melee_web_stage_last_stadium_setup_constructor_begin(map2_object));
 complete_setup_phase(); assert_snapshot_refuses_unchanged("duplicate map-2 constructor callback");

 fixture_setup(3,7); cached_yakumono=&foreign_parameter; capture_setup();
 assert_snapshot_refuses_unchanged("source-local parameter roots or owned seed unavailable");

 fixture_setup(3,7); stage_info.grkind=999; capture_setup();
 assert_snapshot_refuses_unchanged("completed zero-tick prepared Ready owner");

 fixture_setup(3,7); mutate_lease_on_map_init=1; capture_setup();
 assert_snapshot_refuses_unchanged("constructor call/result/owner receipt changed");
 fixture_setup(3,7); mutate_lease_on_map_init=2; capture_setup();
 assert_snapshot_refuses_unchanged("constructor call/result/owner receipt changed");
 puts("PASS component checks only: extracted StageLast receipt functions; Setup zero-tick; refusals preserve output; missing/duplicate constructor; source-owner/grkind; stale GObj/Ground generations; both randi branches and equal-endpoint no-call");
 return 0;
}
'''


class StadiumSetupReceiptControlTests(OwnedWorkspaceTests):
    def test_actual_setup_receipt_functions_and_original_constructor(self):
        stage_path = SOURCE_ROOT / "src/gameplay_stage_last.c"
        stadium_path = SOURCE_ROOT / "build/gameplay-source/src/melee/gr/grpstadium.c"
        if not stage_path.is_file():
            self.fail("Tracked StageLast source is required")
        if not stadium_path.is_file():
            self.skipTest("The checked generated gameplay source tree is not prepared")
        stage = stage_path.read_text(encoding="utf-8")
        stadium = stadium_path.read_text(encoding="utf-8")
        stage_functions = (
            "static int stadium_go_alignment_has_no_events(",
            "static void stadium_setup_fail(",
            "static int stadium_setup_owner_phase(",
            "static int stadium_setup_same_lease(",
            "static int stadium_setup_live_map2(",
            "int melee_web_stage_last_stadium_setup_constructor_begin(",
            "void melee_web_stage_last_stadium_setup_constructor_finish(",
            "int melee_web_stage_last_stadium_setup_snapshot(",
        )
        stadium_functions = (
            "static inline int randi(",
            "void grStadium_801D13E0(Ground_GObj* gobj)",
        )
        program = PRELUDE
        program += "\n".join(_function(stage, signature) for signature in stage_functions)
        program += "\n".join(_function(stadium, signature) for signature in stadium_functions)
        program += TEST_MAIN

        workspace = self.new_workspace(ROOT, "stadium-setup-receipt-control-")
        c_path = workspace / "controls.c"
        binary = workspace / "controls"
        c_path.write_text(program, encoding="utf-8")
        compiler = shutil.which(os.environ.get("CC", "clang")) or shutil.which("cc")
        if compiler is None:
            self.skipTest("native C compiler required")
        command = [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", str(c_path), "-o", str(binary)]
        (workspace / "command.json").write_text(json.dumps(command, indent=2) + "\n",
                                                 encoding="utf-8")
        compile_result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (workspace / "compile.stdout").write_text(compile_result.stdout, encoding="utf-8")
        (workspace / "compile.stderr").write_text(compile_result.stderr, encoding="utf-8")
        self.assertEqual(compile_result.returncode, 0,
                         compile_result.stdout + compile_result.stderr)
        run_result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        (workspace / "run.stdout").write_text(run_result.stdout, encoding="utf-8")
        (workspace / "run.stderr").write_text(run_result.stderr, encoding="utf-8")
        self.assertEqual(run_result.returncode, 0, run_result.stdout + run_result.stderr)
        print(run_result.stdout, end="", flush=True)
        print(f"Control artifacts: {workspace}", flush=True)


if __name__ == "__main__":
    unittest.main()
