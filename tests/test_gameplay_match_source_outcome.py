"""Exercise the patched source-frame adapter's terminal-outcome boundary."""

from pathlib import Path
import shutil
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/melee-gameplay.patch"


def patched_source_frame_adapter(function="melee_web_match_source_frame"):
    lines = PATCH.read_text(encoding="utf-8").splitlines()
    start = next(
        (index for index, line in enumerate(lines)
         if line.startswith(f"+int {function}(")),
    )
    body = []
    depth = 0
    opened = False
    for line in lines[start:]:
        if not line.startswith("+"):
            raise AssertionError("source-frame adapter is not one contiguous patch addition")
        source_line = line[1:]
        body.append(source_line)
        depth += source_line.count("{") - source_line.count("}")
        opened |= "{" in source_line
        if opened and depth == 0:
            return "\n".join(body) + "\n"
    raise AssertionError("source-frame adapter end not found in patch")


FIXTURE = r"""
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

enum { GM_VS = 1, GM_OPENING_MV = 2, MatchKind_Stock = 3, GS_VS=4, GS_SUDDEN_DEATH=5 };
enum {
    OUTCOME_NONE = 0,
    OUTCOME_TIMEOUT = 1,
    OUTCOME_ELIMINATION = 2,
    OUTCOME_TEAM_ELIMINATION = 3,
    OUTCOME_NO_CONTEST = 7,
};

typedef struct {
    int is_vs, match_kind, x4_4, on_frame_start, on_frame_end;
    int x3_2, is_teams, x6;
} StartMeleeRules;
typedef struct {
    StartMeleeRules x24C8;
    int is_singleplayer, unk_0, match_result;
} lbl_8046B6A0_t;

typedef struct { int unk_10_b1; } MatchRuntime;
lbl_8046B6A0_t lbl_8046B6A0;
static MatchRuntime match_runtime;
struct GameSceneInfo { int scene_kind; const void* enter_data; const void* exit_data; };
static struct GameSceneInfo scene;
static struct { StartMeleeRules rules; } gmVsMelee_StartData;
static int gmVsMelee_SuddenDeathExitInfo;
struct GameSceneInfo* melee_web_current_scene_info(void) { return &scene; }
int melee_web_match_source_scene_supported(int,int,const void*,const void*,int,const StartMeleeRules*);
static int game_mode, callback_count, callback_result;
static bool melee_web_opening_demo_active;
int melee_web_match_source_frame(void);

int gm_GetCurrentGameMode(void) { return game_mode; }
MatchRuntime* gm_1601_GetUnkData(void) { return &match_runtime; }
void gm_Scene_Vs_OnFrame(void)
{
    callback_count++;
    lbl_8046B6A0.match_result = callback_result;
}

static void setup(int mode, int teams, int result)
{
    lbl_8046B6A0 = (lbl_8046B6A0_t){0};
    match_runtime = (MatchRuntime){0};
    game_mode = mode;
    scene = (struct GameSceneInfo){GS_VS, NULL, NULL};
    gmVsMelee_StartData.rules.x6 = 0;
    callback_count = 0;
    callback_result = result;
    melee_web_opening_demo_active = false;
    lbl_8046B6A0.x24C8.is_vs = 1;
    lbl_8046B6A0.x24C8.match_kind = MatchKind_Stock;
    lbl_8046B6A0.x24C8.is_teams = teams;
}

static int ordinary(int teams, int result)
{
    setup(GM_VS, teams, result);
    int accepted = melee_web_match_source_frame();
    if (callback_count != 1) return -1;
    return accepted;
}

static int opening_demo(int result)
{
    setup(GM_OPENING_MV, 1, result);
    melee_web_opening_demo_active = true;
    int accepted = melee_web_match_source_frame();
    if (callback_count != 1) return -1;
    return accepted;
}

static int rejected_precondition(int which)
{
    setup(GM_VS, 1, OUTCOME_TEAM_ELIMINATION);
    switch (which) {
    case 0: lbl_8046B6A0.x24C8.is_vs = 0; break;
    case 1: game_mode = GM_OPENING_MV; break;
    case 2: lbl_8046B6A0.x24C8.match_kind = 0; break;
    case 3: lbl_8046B6A0.x24C8.x4_4 = 1; break;
    case 4: lbl_8046B6A0.is_singleplayer = 1; break;
    case 5: lbl_8046B6A0.x24C8.on_frame_start = 1; break;
    case 6: lbl_8046B6A0.x24C8.on_frame_end = 1; break;
    case 7: lbl_8046B6A0.x24C8.x3_2 = 1; break;
    case 8: match_runtime.unk_10_b1 = 1; break;
    case 9: lbl_8046B6A0.unk_0 = 2; break;
    default: return 0;
    }
    return melee_web_match_source_frame() == 0 && callback_count == 0;
}

int main(void)
{
    const int supported[] = {
        OUTCOME_NONE, OUTCOME_TIMEOUT, OUTCOME_ELIMINATION, OUTCOME_NO_CONTEST,
    };
    for (unsigned i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (ordinary(0, supported[i]) != 1 || ordinary(1, supported[i]) != 1)
            return 10 + (int)i;
    }
    if (ordinary(1, OUTCOME_TEAM_ELIMINATION) != 1) return 20;
    if (ordinary(0, OUTCOME_TEAM_ELIMINATION) != 0) return 21;
    if (ordinary(1, 4) != 0 || ordinary(0, 99) != 0) return 22;
    if (opening_demo(OUTCOME_TEAM_ELIMINATION) != 0 ||
        opening_demo(OUTCOME_NO_CONTEST) != 1) return 23;
    for (int i = 0; i < 10; ++i)
        if (!rejected_precondition(i)) return 30 + i;
    // A bare SD bit, wrong scene or foreign payload must not enter callbacks.
    for (int broken=0; broken<5; ++broken) {
        setup(GM_VS,0,OUTCOME_ELIMINATION);
        scene=(struct GameSceneInfo){GS_SUDDEN_DEATH,&gmVsMelee_StartData,
            &gmVsMelee_SuddenDeathExitInfo};
        gmVsMelee_StartData.rules.x6=lbl_8046B6A0.x24C8.x6=1;
        lbl_8046B6A0.x24C8.is_vs=0;
        switch(broken) {
        case 0: scene.scene_kind=GS_VS; break;
        case 1: scene.enter_data=&scene; break;
        case 2: scene.exit_data=&scene; break;
        case 3: gmVsMelee_StartData.rules.x6=0; break;
        case 4: lbl_8046B6A0.x24C8.x6=0; break;
        }
        if(melee_web_match_source_frame()!=0 || callback_count!=0) return 50+broken;
    }
    setup(GM_VS,0,OUTCOME_ELIMINATION);
    scene=(struct GameSceneInfo){GS_SUDDEN_DEATH,&gmVsMelee_StartData,
        &gmVsMelee_SuddenDeathExitInfo};
    gmVsMelee_StartData.rules.x6=lbl_8046B6A0.x24C8.x6=1;
    lbl_8046B6A0.x24C8.is_vs=0;
    if(!melee_web_match_source_frame() || callback_count!=1) return 60;
    puts("source-frame adapter accepts team elimination only for Teams matches");
    return 0;
}
"""



class GameplayMatchSourceOutcomeTests(OwnedWorkspaceTests):
    def test_extracted_adapter_admits_team_elimination_only_for_teams(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("a C compiler is required")
        directory = self.new_workspace(ROOT, "melee-match-outcome-")
        work = Path(directory)
        source = work / "match_source_outcome.c"
        binary = work / "match_source_outcome"
        source.write_text(
            FIXTURE
            + patched_source_frame_adapter("melee_web_match_source_scene_supported")
            + patched_source_frame_adapter(),
            encoding="utf-8",
        )
        result = subprocess.run(
            [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
             str(source), "-o", str(binary)],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True,
                             timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("only for Teams matches", run.stdout)


if __name__ == "__main__":
    unittest.main()
