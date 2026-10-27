"""Focused source-owner controls for the opt-in first-Water graph candidate.

The reducer control is synthetic local pointer identity only. The source test
reconstructs the exact pinned upstream TU plus candidate patch; neither test is
a browser or original-game parity claim.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(os.environ.get("MELEE_WEB_CANDIDATE_SOURCE_ROOT", Path(__file__).resolve().parents[1]))
REFERENCE = Path(os.environ.get("MELEE_WEB_REFERENCE_SOURCE_ROOT", SOURCE))

def body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

def braced_statement(source, start):
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

class StadiumFirstWaterGraphTests(unittest.TestCase):
    def test_fixed_water_reducer_order_and_wrong_identity_refusal(self):
        cc = shutil.which("clang") or shutil.which("cc")
        if not cc:
            self.skipTest("Native C compiler required")
        scratch = Path(tempfile.mkdtemp(prefix="stadium-first-water-reducer-"))
        passed = False
        try:
            code = r"""
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include "gameplay_stadium_display_owner.h"
#include <assert.h>

int main(void) {
    HSD_GObj *base=(HSD_GObj*)(uintptr_t)16;
    HSD_GObj *water=(HSD_GObj*)(uintptr_t)32;
    HSD_GObj *child7=(HSD_GObj*)(uintptr_t)48;
    HSD_GObj *child8=(HSD_GObj*)(uintptr_t)64;
    HSD_GObj *foreign=(HSD_GObj*)(uintptr_t)80;
    const unsigned kinds[11]={1,1,1,2,3,2,2,3,2,3,3};
    const int ids[11]={9,7,8,5,5,9,7,7,8,8,9};
    HSD_GObj *objects[11]={water,child7,child8,base,base,water,child7,child7,
                           child8,child8,water};
    MeleeWebStadiumFirstTransformJournal initial={0};
    initial.armed=1;
    initial.world_generation=9;
    initial.selected_form=MELEE_WEB_STADIUM_FIRST_TRANSFORM_WATER_FORM;
    initial.initial_map5=base;
    assert(melee_web_stadium_first_transform_event_count(&initial)==11);
    MeleeWebStadiumFirstTransformJournal j=initial;
    for (unsigned i=0;i<11;i++) {
        assert(melee_web_stadium_first_transform_record(&j,kinds[i],ids[i],objects[i]));
        assert(j.count==i+1&&!j.failed);
    }
    assert(j.count==11&&j.water_map9==water&&j.water_child7==child7&&
           j.water_child8==child8);
    assert(j.events[0].kind==1&&j.events[0].map_id==9&&j.events[0].object==water);
    assert(j.events[10].kind==3&&j.events[10].map_id==9&&j.events[10].object==water);
    assert(!melee_web_stadium_first_transform_record(&j,3,9,water)&&
           j.failed&&j.count==11);

    j=initial;
    assert(melee_web_stadium_first_transform_record(&j,1,9,water));
    assert(!melee_web_stadium_first_transform_record(&j,1,8,child7)&&j.failed&&j.count==1);

    j=initial;
    assert(melee_web_stadium_first_transform_record(&j,1,9,water));
    assert(!melee_web_stadium_first_transform_record(&j,1,7,water)&&j.failed&&j.count==1);

    j=initial;
    assert(melee_web_stadium_first_transform_record(&j,1,9,water));
    assert(melee_web_stadium_first_transform_record(&j,1,7,child7));
    assert(!melee_web_stadium_first_transform_record(&j,1,8,child7)&&j.failed&&j.count==2);

    j=initial;
    assert(melee_web_stadium_first_transform_record(&j,1,9,water));
    assert(melee_web_stadium_first_transform_record(&j,1,7,child7));
    assert(melee_web_stadium_first_transform_record(&j,1,8,child8));
    assert(melee_web_stadium_first_transform_record(&j,2,5,base));
    assert(melee_web_stadium_first_transform_record(&j,3,5,base));
    assert(!melee_web_stadium_first_transform_record(&j,2,9,foreign)&&j.failed&&j.count==5);

    j=initial;
    j.selected_form=MELEE_WEB_STADIUM_FIRST_TRANSFORM_FIRE_FORM;
    assert(melee_web_stadium_first_transform_event_count(&j)==5);
    assert(!melee_web_stadium_first_transform_record(&j,1,9,water)&&j.failed&&j.count==0);

    j=initial;
    j.selected_form=4;
    assert(melee_web_stadium_first_transform_event_count(&j)==0);
    assert(!melee_web_stadium_first_transform_record(&j,1,9,water)&&j.failed&&j.count==0);
    return 0;
}
"""
            (scratch / "water_reducer.c").write_text(code)
            compile_result = subprocess.run(
                [cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(SOURCE / "src"), "-I", str(REFERENCE / "src"),
                 str(scratch / "water_reducer.c"), "-o", str(scratch / "water_reducer")],
                capture_output=True, text=True, timeout=30)
            (scratch / "compile.stdout").write_text(compile_result.stdout)
            (scratch / "compile.stderr").write_text(compile_result.stderr)
            self.assertEqual(compile_result.returncode, 0,
                             str(scratch) + "\n" + compile_result.stdout + compile_result.stderr)
            run_result = subprocess.run([str(scratch / "water_reducer")],
                                        capture_output=True, text=True, timeout=30)
            (scratch / "run.stdout").write_text(run_result.stdout)
            (scratch / "run.stderr").write_text(run_result.stderr)
            self.assertEqual(run_result.returncode, 0,
                             str(scratch) + "\n" + run_result.stdout + run_result.stderr)
            passed = True
        finally:
            if passed:
                shutil.rmtree(scratch)
            else:
                print("Retained Water reducer failure: " + str(scratch), flush=True)

    def test_actual_patched_water_source_order_owner_guards_and_terminal(self):
        git = shutil.which("git")
        cc = shutil.which("clang") or shutil.which("cc")
        if not cc:
            self.skipTest("Native C compiler required")
        upstream = REFERENCE / ".deps" / "melee"
        if not git or not (upstream / ".git").exists():
            self.skipTest("Pinned Melee source required")
        lock = __import__("json").loads((REFERENCE / "dependencies.lock.json").read_text())
        commit = lock["repositories"]["melee"]["commit"]
        actual = subprocess.check_output([git, "rev-parse", "HEAD"], cwd=upstream,
                                         text=True).strip()
        self.assertEqual(actual, commit)
        scratch = Path(tempfile.mkdtemp(prefix="stadium-first-water-source-"))
        passed = False
        try:
            for relative in ("src/melee/gr/grpstadium.c", "src/melee/gr/ground.c"):
                output = scratch / relative
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_bytes(subprocess.check_output(
                    [git, "show", commit + ":" + relative], cwd=upstream))
            apply = subprocess.run(
                [git, "apply", "--no-index",
                 "--include=src/melee/gr/grpstadium.c",
                 "--include=src/melee/gr/ground.c",
                 str(SOURCE / "patches" / "melee-gameplay.patch")],
                cwd=scratch, capture_output=True, text=True, timeout=30)
            (scratch / "apply.stdout").write_text(apply.stdout)
            (scratch / "apply.stderr").write_text(apply.stderr)
            self.assertEqual(apply.returncode, 0,
                             str(scratch) + "\n" + apply.stdout + apply.stderr)
            stadium = (scratch / "src/melee/gr/grpstadium.c").read_text()
            ground = (scratch / "src/melee/gr/ground.c").read_text()

            construct = body(stadium, "void grStadium_801D1A38(")
            self.assertLess(construct.index("Ground_801C2ED0(jobj, gp->map_id);"),
                            construct.index("grStadium_801D10F8(7)"))
            self.assertLess(construct.index("grStadium_801D10F8(7)"),
                            construct.index("grStadium_801D10F8(8)"))

            graph = body(stadium, "static int stadium_first_water_graph_matches(")
            for required in (
                "j->selected_form!=MELEE_WEB_STADIUM_FIRST_TRANSFORM_WATER_FORM",
                "j->count!=0&&j->count!=3&&j->count!=5",
                "map2->u.stadium.xC4_b1",
                "const unsigned ids[7]={0,1,2,5,9,7,8}",
                "stadium_owner_water_proc_chain_matches(j->water_map9",
                "stadium_owner_water_proc_chain_matches(j->water_child7",
                "stadium_owner_water_proc_chain_matches(j->water_child8",
                "retiring&&(Ground_GetMapGObj(2)!=owner->map2_ground||j->count!=5)",
            ):
                self.assertIn(required, graph)
            self.assertNotIn("if(j->count==0){", graph,
                             "initial phase0 invariants must not reject request/loaded phases")

            common_start = graph.index(
                "if(form!=MELEE_WEB_STADIUM_FIRST_TRANSFORM_WATER_FORM||")
            common_end = graph.index("return 0;", common_start) + len("return 0;")
            common_guard = graph[common_start:common_end]
            phase_start = graph.index("if(phase==1){")
            phase_one = braced_statement(graph, phase_start)
            else_start = graph.index("else{", phase_start + len(phase_one))
            phase_other = braced_statement(graph, else_start)
            phase_chain = phase_one + " " + phase_other
            # Compile and execute the literal phase guards from the production
            # graph body with tiny field stubs, without a whole Ground fixture.
            guard_code = r"""
#include <assert.h>
#define MELEE_WEB_STADIUM_FIRST_TRANSFORM_WATER_FORM 9
typedef struct { int xDC,xDE,xE0,xE2,xC4_b1; void *xD0,*xE4,*xE8; } StadiumFields;
typedef struct { StadiumFields stadium; } GroundUnion;
typedef struct { GroundUnion u; } Ground;
typedef struct { unsigned map2_buffer_use_count; } Owner;
typedef struct { unsigned count; void *source_slot,*initial_map5,*water_map9; } Journal;
static void *archive9;
static void *melee_web_stage_map_lookup(int id) { return id==9 ? archive9 : 0; }
static int phase_gate(int phase,int form,Journal *j,Owner *owner,Ground *map2) {
""" + common_guard + "\n" + phase_chain + r"""
 return 1;
}
int main(void) {
 void *base=(void*)16,*slot=(void*)32,*water=(void*)48,*other=(void*)64;
 Journal j={.count=0,.source_slot=0,.initial_map5=base,.water_map9=0};
 Owner owner={1}; Ground m={0};
 m.u.stadium.xE0=5; m.u.stadium.xE2=-1; m.u.stadium.xE4=base; archive9=slot;
 assert(phase_gate(1,9,&j,&owner,&m));
 j.count=1; assert(!phase_gate(1,9,&j,&owner,&m));
 j.count=0; owner.map2_buffer_use_count=2; assert(!phase_gate(1,9,&j,&owner,&m));
 owner.map2_buffer_use_count=1; m.u.stadium.xD0=slot; assert(!phase_gate(1,9,&j,&owner,&m));

 for (int phase=2;phase<=4;phase++) {
  j=(Journal){.count=0,.source_slot=0,.initial_map5=base,.water_map9=0};
  owner.map2_buffer_use_count=2;
  m=(Ground){0}; m.u.stadium.xE0=5; m.u.stadium.xE2=-1;
  m.u.stadium.xE4=base; m.u.stadium.xD0=slot;
  assert(phase_gate(phase,9,&j,&owner,&m));
  assert(j.source_slot==slot);
  j=(Journal){.count=3,.source_slot=0,.initial_map5=base,.water_map9=0};
  assert(!phase_gate(phase,9,&j,&owner,&m));
  j=(Journal){.count=0,.source_slot=0,.initial_map5=base,.water_map9=0};
  owner.map2_buffer_use_count=1;
  assert(!phase_gate(phase,9,&j,&owner,&m));
  owner.map2_buffer_use_count=2; m.u.stadium.xC4_b1=1;
  assert(!phase_gate(phase,9,&j,&owner,&m));
  m.u.stadium.xC4_b1=0; m.u.stadium.xD0=other;
  assert(!phase_gate(phase,9,&j,&owner,&m));
  m.u.stadium.xD0=slot; m.u.stadium.xE4=water;
  assert(!phase_gate(phase,9,&j,&owner,&m));
 }
 j=(Journal){.count=0,.source_slot=0,.initial_map5=base,.water_map9=0};
 owner.map2_buffer_use_count=2; m=(Ground){0};
 m.u.stadium.xE4=base; m.u.stadium.xD0=slot; m.u.stadium.xE0=4; m.u.stadium.xE2=-1;
 assert(!phase_gate(2,9,&j,&owner,&m));
 m.u.stadium.xE0=5; m.u.stadium.xE2=0;
 assert(!phase_gate(2,9,&j,&owner,&m));
 assert(!phase_gate(2,3,&j,&owner,&m));
 assert(!phase_gate(7,9,&j,&owner,&m));
 j=(Journal){3,slot,base,water}; owner.map2_buffer_use_count=2; m=(Ground){0};
 m.u.stadium.xD0=slot; m.u.stadium.xE8=water; m.u.stadium.xE4=base;
 m.u.stadium.xE0=5; m.u.stadium.xE2=-1;
 assert(phase_gate(5,9,&j,&owner,&m));
 j.count=2; assert(!phase_gate(5,9,&j,&owner,&m));
 j=(Journal){5,slot,base,water}; m.u.stadium.xE8=0; m.u.stadium.xE4=water;
 assert(phase_gate(6,9,&j,&owner,&m));
 j.count=3; assert(!phase_gate(6,9,&j,&owner,&m));
 return 0;
}
"""
            gate_scratch = scratch / "water_phase_gate.c"
            gate_scratch.write_text(guard_code)
            gate_compile = subprocess.run(
                [cc,"-std=c11","-Wall","-Wextra","-Werror",str(gate_scratch),
                 "-o",str(scratch/"water_phase_gate")],
                capture_output=True,text=True,timeout=30)
            (scratch/"water_phase_gate.compile.stdout").write_text(gate_compile.stdout)
            (scratch/"water_phase_gate.compile.stderr").write_text(gate_compile.stderr)
            self.assertEqual(gate_compile.returncode,0,
                             str(scratch)+"\n"+gate_compile.stdout+gate_compile.stderr)
            gate_run=subprocess.run([str(scratch/"water_phase_gate")],
                                    capture_output=True,text=True,timeout=30)
            (scratch/"water_phase_gate.run.stdout").write_text(gate_run.stdout)
            (scratch/"water_phase_gate.run.stderr").write_text(gate_run.stderr)
            self.assertEqual(gate_run.returncode,0,
                             str(scratch)+"\n"+gate_run.stdout+gate_run.stderr)

            callback = body(stadium, "int melee_web_stadium_water_graph_callback_matches(")
            for required in (
                "callback==fn_801D4220",
                "j->selected_form==MELEE_WEB_STADIUM_FIRST_TRANSFORM_WATER_FORM",
                "j->count==0",
                "owner->map2_buffer_use_count==1",
                "stadium_owner_map2_buffer_allocation_matches",
            ):
                self.assertIn(required, callback)

            end = body(stadium, "int melee_web_stadium_display_owner_first_transform_end(")
            guards = [
                "j->count!=expected",
                "Ground_GetMapGObj(2)||Ground_GetMapGObj(5)",
                "Ground_GetMapGObj(9)||Ground_GetMapGObj(7)||Ground_GetMapGObj(8)",
                "melee_web_stage_map_lookup(",
            ]
            self.assertLess(max(end.index(token) for token in guards),
                            end.rindex("grAnime_801C65B0(j->source_slot)"))

            retire = body(ground, "void Ground_801C4A08(")
            entering = retire.index("melee_web_stadium_display_owner_note_ground_retire(gobj,1);")
            source_callback = retire.index("gp->x1C_callback(gobj)")
            unlink = retire.index("HSD_GObjPLink_80390228(gobj)")
            returning = retire.index("melee_web_stadium_display_owner_note_ground_retire(gobj,0);")
            self.assertLess(entering, source_callback)
            self.assertLess(source_callback, unlink)
            self.assertLess(unlink, returning)

            water_predicate = body(stadium, "int melee_web_stadium_water_graph_callback_matches(")
            self.assertNotIn("grStadium_801D4220(", water_predicate,
                             "predicate must identify the callback without invoking it")
            passed = True
        finally:
            if passed:
                shutil.rmtree(scratch)
            else:
                print("Retained Water source reconstruction: " + str(scratch), flush=True)

if __name__ == "__main__":
    unittest.main()

