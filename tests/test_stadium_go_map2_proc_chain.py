"""Small actual-validator reducer; synthetic registries, no gameplay lifetime."""
from pathlib import Path
import shutil
import subprocess
import unittest
import json

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class StadiumGoMap2ProcChainTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("clang") or shutil.which("cc")
        # External proposal uses its canonical candidate; ordinary checkout
        # controls require prepared generated source and skip when unavailable.
        source_root = ROOT / "src"
        if not (source_root / "melee/gr/grpstadium.c").is_file():
            source_root = ROOT / "build/gameplay-source/src"
        cls.stadium_source = source_root / "melee/gr/grpstadium.c"
        cls.ground_source = source_root / "melee/gr/ground.c"
        if not cls.compiler or not (cls.stadium_source.is_file() and cls.ground_source.is_file()):
            raise unittest.SkipTest("Prepared pinned source and native compiler required")
        cls.scratch = cls.new_workspace(ROOT, "stadium-map2-proc-chain-")

    def test_authored_three_proc_chain_and_rejected_foreign_children(self):
        stadium = self.stadium_source.read_text()
        ground = self.ground_source.read_text()
        signatures = [
            "static int stadium_owner_map2_proc_root_contains_once(",
            "static int stadium_owner_go_proc_root_contains_once(",
            "static int stadium_owner_go_map2_proc_chain_matches(",
        ]
        prelude = r"""
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct HSD_GObj HSD_GObj;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef struct HSD_GObjProc {
    struct HSD_GObjProc *next, *child;
    HSD_GObj *gobj;
    HSD_GObjEvent on_invoke;
    unsigned s_link;
} HSD_GObjProc;
struct HSD_GObj { HSD_GObjProc *proc; };
static HSD_GObjProc *roots[11];
static HSD_GObjProc **HSD_GObj_804D7840 = roots;
static struct { unsigned gproc_pri_max; } HSD_GObjLibInitData = {10};
enum { STADIUM_MAP2_SNAPSHOT_TRAVERSAL_LIMIT = 4096 };
static void Ground_801C1CD0(HSD_GObj *g) {(void)g;}
static void Ground_801C1D38(HSD_GObj *g) {(void)g;}
static void grStadium_801D1520(HSD_GObj *g) {(void)g;}
static void foreign_callback(HSD_GObj *g) {(void)g;}
"""
        harness = r"""
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    HSD_GObj map = {0}, foreign = {0};
    HSD_GObjProc head = {0}, middle = {0}, tail = {0};
    head.gobj = middle.gobj = tail.gobj = &map;
    head.on_invoke = grStadium_801D1520;
    middle.on_invoke = Ground_801C1D38;
    tail.on_invoke = Ground_801C1CD0;
    head.s_link = middle.s_link = 4; tail.s_link = 1;
    head.child = &middle; middle.child = &tail;
    map.proc = &head; roots[4] = &head; head.next = &middle; roots[1] = &tail;
    REQUIRE(stadium_owner_go_map2_proc_chain_matches(&map, &head));
    head.child = NULL;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); head.child = &middle;
    head.child = (HSD_GObjProc*)(uintptr_t)1;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); head.child = &middle;
    middle.child = (HSD_GObjProc*)(uintptr_t)1;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); middle.child = &tail;
    middle.on_invoke = foreign_callback;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); middle.on_invoke = Ground_801C1D38;
    tail.on_invoke = Ground_801C1D38;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); tail.on_invoke = Ground_801C1CD0;
    tail.gobj = &foreign;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); tail.gobj = &map;
    middle.s_link = 1;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); middle.s_link = 4;
    tail.child = &head;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); tail.child = NULL;
    middle.child = &head;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); middle.child = &tail;
    middle.next = &head;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &head)); middle.next = NULL;
    REQUIRE(!stadium_owner_go_map2_proc_chain_matches(&map, &middle));
    REQUIRE(stadium_owner_go_map2_proc_chain_matches(&map, &head));
    puts("actual map2 chain validator: positive and 11 negative controls passed; synthetic registries only");
    return 0;
}
"""
        code = prelude + function(ground,
            "int melee_web_stadium_c1_ground_proc_callback_matches(")
        code += "\n".join(function(stadium, signature) for signature in signatures)
        code += harness
        src, binary = self.scratch / "control.c", self.scratch / "control"
        src.write_text(code)
        compile_command = [self.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                           str(src), "-o", str(binary)]
        (self.scratch / "compile-command.json").write_text(json.dumps(compile_command) + "\n")
        with (self.scratch / "compile.stdout").open("w") as stdout, (self.scratch / "compile.stderr").open("w") as stderr:
            compiled = subprocess.run(compile_command, stdout=stdout, stderr=stderr, timeout=30)
        compile_output = (self.scratch / "compile.stdout").read_text() + (self.scratch / "compile.stderr").read_text()
        self.assertEqual(compiled.returncode, 0, compile_output)
        with (self.scratch / "run.stdout").open("w") as stdout, (self.scratch / "run.stderr").open("w") as stderr:
            checked = subprocess.run([str(binary)], stdout=stdout, stderr=stderr, timeout=5)
        run_output = (self.scratch / "run.stdout").read_text() + (self.scratch / "run.stderr").read_text()
        self.assertEqual(checked.returncode, 0, run_output)
        self.assertIn("positive and 11 negative controls passed", run_output)



if __name__ == "__main__":
    unittest.main()
