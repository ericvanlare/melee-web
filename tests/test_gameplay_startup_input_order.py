"""Compile the production world-entry boundary with strict PAD/selection owners.

The real-asset content trace separately checks held-A Zelda/Sheik startup. This
small reducer catches restoring menu input after source player selection again.
"""

from pathlib import Path
import shutil
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]


def world_entry():
    source = (ROOT / "src/gameplay_world.cpp").read_text()
    start = source.index("            match_context=melee_web_match_begin_players(")
    end = source.index("            const MeleeWebArchiveSymbol refract_symbol=", start)
    return source[start:end]


PREFIX = r'''
#include <array>
#include <cassert>
#include <stdexcept>
struct Input { int button; };
struct Selection {
    std::array<int, 4> source_players{};
    unsigned player_count=2, source_camera_subjects=70, source_random_seed=123;
    bool opening_demo=false, sudden_death=false;
    const Input* source_initial_input=nullptr;
};
int context, camera, rules_owner, menu_owner;
int pad=-1, form=-1, restore_count=0, prepare_count=0, sd_prepare_count=0;
bool opening_mode_seen=false;
bool reject_restore=false;
void check(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
int* melee_web_match_begin_players(const int*, unsigned, unsigned, unsigned,
                                   void*, char*, unsigned long) {
    pad=0; form=-1; restore_count=prepare_count=sd_prepare_count=0;
    return &context;
}
int melee_web_match_restore_input(int* owner, const Input* input, char*, unsigned long) {
    assert(owner==&context && prepare_count==0 && restore_count==0);
    if (reject_restore) return 0;
    pad=input->button; ++restore_count; return 1;
}
int* melee_web_render_prepare_match_camera(char*, unsigned long) { return &camera; }
int melee_web_match_rules_prepare_from_menu(int* rules, const int* menu,
                                            bool opening_demo, char*, unsigned long) {
    assert(rules==&rules_owner && menu==&menu_owner);
    ++prepare_count;
    opening_mode_seen=opening_demo;
    // fn_8016D8AC consumes HSD_PadCopyStatus at this boundary, not later
    // during Fighter_Create or deferred spawn-matrix evaluation.
    form=pad; return 1;
}
int melee_web_match_rules_prepare_sudden_death_from_menu(int* rules,
        const int* menu, char* error, unsigned long size) {
    ++sd_prepare_count;
    return melee_web_match_rules_prepare_from_menu(rules, menu, false, error, size);
}
void enter(const Selection& selection, const int* source_start_data) {
    char error[64]="input rejected";
    int* match_context=nullptr;
    int* render_context=nullptr;
    int* rules=&rules_owner;
'''


SUFFIX = r'''
    assert(match_context==&context && render_context==&camera);
    assert(opening_mode_seen==selection.opening_demo);
    assert(sd_prepare_count==(selection.sudden_death ? 1 : 0));
}
int main() {
    Selection selected;
    Input held_a{0x100};
    selected.source_initial_input=&held_a;
    enter(selected,&menu_owner);
    assert(form==0x100 && restore_count==1 && prepare_count==1);
    selected.opening_demo=true;
    enter(selected,&menu_owner);
    assert(form==0x100 && restore_count==1 && prepare_count==1);
    selected.opening_demo=false;
    selected.sudden_death=true;
    enter(selected,&menu_owner);
    assert(form==0x100 && restore_count==1 && prepare_count==1);
    selected.sudden_death=false;
    selected.source_initial_input=nullptr;
    enter(selected,&menu_owner);
    assert(form==0 && restore_count==0 && prepare_count==1);
    selected.source_initial_input=&held_a;
    reject_restore=true;
    bool rejected=false;
    try { enter(selected,&menu_owner); }
    catch (const std::runtime_error&) { rejected=true; }
    assert(rejected && prepare_count==0);
}
'''



class GameplayStartupInputOrderTests(OwnedWorkspaceTests):
    def test_pre_collision_restore_retains_world_and_one_shot_guards(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("a C compiler is required")
        source = (ROOT / "src/gameplay_match_context.c").read_text()
        owned = source[source.index("static int owned("):
                       source.index("MeleeWebMatchContext* melee_web_match_begin(")]
        restore = source[source.index("int melee_web_match_restore_input("):
                         source.index("int melee_web_match_set_player_start(")]
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned button; } MeleeWebPadState;
typedef struct {
    uint64_t generation,ticks;
    unsigned seed,input_restored,player_count,slots[4];
    void* pool;
} MeleeWebMatchContext;
typedef struct { void* player_entity[2]; } Player;
static MeleeWebMatchContext* owner;
static unsigned* seed_ptr;
static void* cm_804D645C;
static unsigned generation=7,applied;
static struct { unsigned qcount; } HSD_PadLibData;
static Player players[4];
static uint64_t melee_web_gameplay_generation(void){return generation;}
static Player* Player_GetPtrForSlot(unsigned slot){return &players[slot];}
static void melee_web_pad_state_apply(const MeleeWebPadState* state){applied=state->button;}
static int fail(char* e,size_t n,const char* message){(void)e;(void)n;(void)message;return 0;}
static int ok(char* e,size_t n){(void)e;(void)n;return 1;}
''' + owned + restore + r'''
int main(void){
    MeleeWebMatchContext context={.generation=7,.player_count=1};
    MeleeWebPadState input={.button=0x100};
    owner=&context;seed_ptr=&context.seed;cm_804D645C=context.pool;
    assert(!melee_web_match_restore_input(NULL,&input,NULL,0));
    ++generation;assert(!melee_web_match_restore_input(owner,&input,NULL,0));--generation;
    seed_ptr=NULL;assert(!melee_web_match_restore_input(owner,&input,NULL,0));seed_ptr=&context.seed;
    cm_804D645C=&input;assert(!melee_web_match_restore_input(owner,&input,NULL,0));cm_804D645C=context.pool;
    assert(!melee_web_match_restore_input(owner,NULL,NULL,0));
    context.ticks=1;assert(!melee_web_match_restore_input(owner,&input,NULL,0));context.ticks=0;
    HSD_PadLibData.qcount=1;assert(!melee_web_match_restore_input(owner,&input,NULL,0));HSD_PadLibData.qcount=0;
    players[0].player_entity[0]=&input;
    assert(!melee_web_match_restore_input(owner,&input,NULL,0));players[0].player_entity[0]=NULL;
    assert(applied==0 && context.input_restored==0);
    assert(melee_web_match_restore_input(owner,&input,NULL,0));
    assert(applied==0x100 && context.input_restored==1);
    assert(!melee_web_match_restore_input(owner,&input,NULL,0));
}
'''
        directory = self.new_workspace(ROOT, 'melee-pad-lease-')
        root = Path(directory)
        source, binary = root / "lease.c", root / "lease"
        source.write_text(harness)
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(binary)],
                       check=True, capture_output=True, text=True, timeout=30)
        subprocess.run([str(binary)], check=True, timeout=10)

    def test_input_restored_once_before_source_player_selection(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("a C++ compiler is required")
        directory = self.new_workspace(ROOT, 'melee-startup-input-')
        root = Path(directory)
        source, binary = root / "order.cpp", root / "order"
        source.write_text(PREFIX + world_entry() + SUFFIX)
        compiled = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(binary)],
                       capture_output=True, text=True, timeout=30)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
