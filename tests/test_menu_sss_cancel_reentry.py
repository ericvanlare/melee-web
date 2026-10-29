#!/usr/bin/env python3
"""Exact-source reducer for canceled VS SSS CSS-route re-entry."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import unittest
import sys

ROOT = Path(os.environ.get("MELEE_REPO_ROOT", Path(__file__).resolve().parents[1])).resolve()
HOST = Path(os.environ.get("MELEE_HOST_SOURCE", ROOT / "src/gameplay_menu_host.c")).resolve()
BASELINE_HOST = Path(os.environ["MELEE_BASELINE_HOST_SOURCE"]).resolve() if os.environ.get("MELEE_BASELINE_HOST_SOURCE") else None
EVIDENCE = Path(os.environ["MELEE_TEST_EVIDENCE_DIR"]).resolve() if os.environ.get("MELEE_TEST_EVIDENCE_DIR") else None
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function

GM = ROOT / ".deps/melee/src/melee/gm/gm_1A3F.c"
MODE = ROOT / ".deps/melee/src/melee/gm/gmvsmode.c"
MODE_H = ROOT / ".deps/melee/src/melee/gm/gmvsmode.h"
FORWARD = ROOT / ".deps/melee/src/melee/gm/forward.h"
VS = ROOT / ".deps/melee/src/melee/gm/gmvsmelee.c"
PATCH = ROOT / "patches/melee-gameplay.patch"


def function(source, signature):
    return _function(source, signature)


def get_machine(source):
    a = source.index("struct routingInfo {")
    b = source.index("/* 1A3F48 */ static void preloadState", a)
    return source[a:b]


def get_enum(source):
    a = source.index("typedef enum {")
    b = source.index("} gmVsMode_StateId;", a) + len("} gmVsMode_StateId;")
    return source[a:b]


def get_post_exit(source):
    body = function(source, "void gm_801A4014(GameMode* mode)")
    block = function(body, "if (!gmMainLib_8046B0F0.resetting) {")
    a = block.index("state_machine.routing.prev_state_id = sm->routing.curr_state_id;")
    return block[a:block.rfind("}")].strip()


def get_authored_rows(source):
    a = source.index("GameModeState gm_Mode_Vs_States[] = {")
    b = source.index("\nenum {", a)
    rows = re.findall(r"(?m)^    \{\s*([A-Za-z_][A-Za-z0-9_]*)", source[a:b])
    if len(rows) != 9 or rows[:2] != ["gmVsMode_State_Css", "gmVsMode_State_Sss"] or rows[-1] != "GM_GAMEMODESTATE_TERMINATE":
        raise AssertionError(f"authored VS table changed: {rows}")
    return rows


def scene_enum_value(source, name):
    match = re.search(r"(?m)^\s*/\* \+([0-9A-Fa-f]{2}) \*/\s+" + re.escape(name) + r",", source)
    if not match:
        raise AssertionError(f"source scene enum missing {name}")
    return int(match.group(1), 16)


TEMPLATE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
typedef unsigned char u8;
typedef uint64_t u64;
typedef uint32_t u32;
#define ASSERT_SIZE(type,size)
#define GM_GAMEMODESTATE_TERMINATE ((u8)-1)
#define GM_MENU 1
#define TRAINING_MODE 1
#define GM_VS 7
#define GM_TRAINING 8
#define GM_COUNT 32
#define GM_MAX_PLAYERS 6
#define CSSPendingSceneChange_2 2
#define MELEE_WEB_MENU_SCENE_CSS 1
#define MELEE_WEB_MENU_SCENE_SSS 2
#define MELEE_WEB_HOST_SCENE_NONE 0
#define MELEE_WEB_HOST_SCENE_CSS 1
#define MELEE_WEB_HOST_SCENE_SSS 2
#define GS_CSS @GS_CSS@
#define GS_SSS @GS_SSS@
@ENUM@
typedef struct { int ckind; } PlayerInit;
typedef struct { int stkind; } GameRules;
typedef struct { GameRules rules; PlayerInit players[6]; } StartMeleeData;
typedef struct { StartMeleeData start; } VsModeData;
typedef struct CSSData { int match_type; int pending_scene_change; void* ko_counts; VsModeData vs; } CSSData;
typedef struct SSSData { int start_game; VsModeData vs; } SSSData;
typedef struct GameModeState GameModeState;
typedef struct { int scene_kind; void* enter_data; void* exit_data; } GameSceneInfo;
struct GameModeState {
    u8 id;
    struct { int scene_kind; void* enter_data; void* exit_data; } info;
    void (*on_enter)(GameModeState*);
    void (*on_exit)(GameModeState*);
};
typedef struct { CSSData css; SSSData sss; } MeleeWebMenuSession;
typedef struct { int active; uint64_t generation; } MeleeWebAudio;
typedef struct { int active; } MeleeWebSaveProfileOwner;
typedef struct {
    int source_scene,source_mode_kind,vs_mode_owned,css_parent_route_requested;
    int source_target_mode,training_start_pending;
    uint64_t generation,audio_generation;
    MeleeWebMenuSession* session;
    MeleeWebAudio* audio;
    MeleeWebSaveProfileOwner* profile;
    GameSceneInfo source_scene_info;
    GameModeState vs_css_state,vs_sss_state,training_css_state,training_sss_state;
} MeleeWebMenuHost;
typedef enum { MENU_SCENE_CSS=MELEE_WEB_MENU_SCENE_CSS,
               MENU_SCENE_SSS=MELEE_WEB_MENU_SCENE_SSS } MeleeWebMenuScene;
typedef struct { uint64_t generation; } GameplayStats;
typedef struct GameMode { GameModeState* states; } GameMode;
@MACHINE@
static struct stateMachine state_machine;
@VS_LEASE_STATE@
static MeleeWebMenuHost* owner;
static uint64_t world_generation;
static int bank_transport_active;
static unsigned css_audio_calls;
static VsModeData vs_mode_data;
static void onExitCss(GameModeState*);
static void onExitSss(GameModeState*);
static void fixture_on_enter(GameModeState*);
static GameModeState gm_Mode_Vs_States[] = {@ROWS@};
static GameModeState gm_Mode_Training_States[] = {
    {.id=0}, {.id=1}, {.id=GM_GAMEMODESTATE_TERMINATE}
};
static GameplayStats melee_web_gameplay_stats(void) { return (GameplayStats){world_generation}; }
static int melee_web_audio_is_active(MeleeWebAudio* a) { return a && a->active; }
static int melee_web_audio_bank_transport_active(void) { return bank_transport_active; }
static int melee_web_save_profile_owner_live(MeleeWebSaveProfileOwner* p,char*e,size_t n) {
    (void)e;(void)n;return p && p->active;
}
static CSSData* melee_web_menu_css(MeleeWebMenuSession* s) { return s ? &s->css : NULL; }
static SSSData* melee_web_menu_sss(MeleeWebMenuSession* s) { return s ? &s->sss : NULL; }
static void* gm_GetGameModeStateExitData(GameModeState* s) { return s ? s->info.exit_data : NULL; }
static VsModeData* gmVsMelee_GetVsData(void) { return &vs_mode_data; }
static uint64_t lbAudioAx_80026E84(int c) {
    (void)c; ++css_audio_calls; return UINT64_C(1) << (css_audio_calls-1);
}
static void lbAudioAx_80026F2C(unsigned x) { (void)x; ++css_audio_calls; }
static void lbAudioAx_8002702C(unsigned x,uint64_t m) { (void)x;(void)m;++css_audio_calls; }
static void lbAudioAx_80027168(void) { ++css_audio_calls; }
static uint64_t lbAudioAx_80026EBC(int x) { return (uint64_t)(unsigned)x; }
static void gm_801A4B88(GameSceneInfo* info) { (void)info; }
static void fixture_on_enter(GameModeState* state) { (void)state; }
@ORIGINAL_AND_HOST_FUNCTIONS@
static int exact_original_post_exit(GameMode* mode, GameModeState* exited_state) {
    struct stateMachine* sm=&state_machine;
    if (exited_state != NULL && exited_state->on_exit != NULL)
        exited_state->on_exit(exited_state);
@POST_EXIT@
    return 0;
}
static MeleeWebMenuHost host;
static MeleeWebMenuSession session;
static MeleeWebAudio audio;
static MeleeWebSaveProfileOwner profile;
static struct stateMachine caller_snapshot;
static void fixture_reset_scene_owner(void) {
    host.source_scene=MELEE_WEB_HOST_SCENE_NONE;
    memset(&host.source_scene_info,0,sizeof(host.source_scene_info));
}
static void initialize(void) {
    memset(&state_machine,0xA7,sizeof(state_machine));
    caller_snapshot=state_machine;
    memset(&host,0,sizeof(host)); memset(&session,0,sizeof(session));
    memset(&audio,0,sizeof(audio)); memset(&profile,0,sizeof(profile));
    memset(&vs_mode_data,0,sizeof(vs_mode_data));
    audio.active=1; audio.generation=71; profile.active=1;
    world_generation=41; bank_transport_active=1; css_audio_calls=0;
    host.session=&session; host.audio=&audio; host.profile=&profile;
    host.generation=world_generation; host.audio_generation=audio.generation;
    host.source_mode_kind=GM_VS; host.source_target_mode=-1;
    owner=&host;
    for(int i=0;i<GM_MAX_PLAYERS;i++) session.css.vs.start.players[i].ckind=i;
    assert(melee_web_vs_mode_begin());
    host.vs_mode_owned=1;
    assert(melee_web_vs_mode_set_route(GM_VS,GM_MENU));
    state_machine.routing.curr_state_id=gmVsMode_State_Css;
    state_machine.routing.prev_state_id=gmVsMode_State_Css;
    session.css.pending_scene_change=1;
}
static void assert_original_route(struct stateMachine before, VsModeData vs_before,
                                  GameModeState* exited_state) {
    struct stateMachine actual=state_machine;
    VsModeData actual_vs=vs_mode_data;
    unsigned actual_audio_calls=css_audio_calls;
    GameMode mode={gm_Mode_Vs_States};
    state_machine=before;
    vs_mode_data=vs_before;
    exact_original_post_exit(&mode,exited_state);
    assert(memcmp(&state_machine.routing,&actual.routing,sizeof(actual.routing))==0);
    state_machine=actual;
    vs_mode_data=actual_vs;
    css_audio_calls=actual_audio_calls;
}
static void enter_scene(MeleeWebMenuScene scene) {
    char error[192]={0};
    fixture_reset_scene_owner();
    assert(source_scene_enter(&host,scene,error,sizeof(error)));
}
#ifndef EXPECT_PREFIX_ONLY
static void cancel_current_sss(void) {
    char error[192]={0};
    struct stateMachine before=state_machine;
    VsModeData vs_before=vs_mode_data;
    session.sss.start_game=0;
    assert(source_scene_exit(&host,MENU_SCENE_SSS,error,sizeof(error)));
    assert_original_route(before,vs_before,&host.vs_sss_state);
    assert(gm_GetPreviousSceneIndex()==gmVsMode_State_Sss);
    assert(gm_GetCurrentSceneIndex()==gmVsMode_State_Css);
    assert(melee_web_vs_mode_next_state()==-1);
}
#endif
static void css_to_sss(void) {
    char error[192]={0};
    struct stateMachine before=state_machine;
    VsModeData vs_before=vs_mode_data;
    session.css.pending_scene_change=1;
    assert(source_scene_exit(&host,MENU_SCENE_CSS,error,sizeof(error)));
    assert_original_route(before,vs_before,&host.vs_css_state);
    assert(gm_GetPreviousSceneIndex()==gmVsMode_State_Css);
    assert(gm_GetCurrentSceneIndex()==gmVsMode_State_Sss);
    assert(melee_web_vs_mode_next_state()==-1);
}
static void finish(void) {
    assert(melee_web_vs_mode_owned);
    assert(melee_web_vs_mode_end());
    assert(!melee_web_vs_mode_owned);
    assert(memcmp(&state_machine,&caller_snapshot,sizeof(state_machine))==0);
}
#ifdef EXPECT_PREFIX_ONLY
static void test_expected_prefix_failure(void) {
    char error[192]={0};
    initialize(); enter_scene(MENU_SCENE_CSS); css_to_sss();
    enter_scene(MENU_SCENE_SSS);
    struct stateMachine before_cancel=state_machine;
    VsModeData vs_before_cancel=vs_mode_data;
    assert(source_scene_exit(&host,MENU_SCENE_SSS,error,sizeof(error)));
    assert(gm_GetCurrentSceneIndex()==gmVsMode_State_Sss);
    assert(melee_web_vs_mode_next_state()==gmVsMode_State_Css);
    struct stateMachine original=state_machine;
    VsModeData vs_after_cancel=vs_mode_data;
    state_machine=before_cancel; vs_mode_data=vs_before_cancel;
    GameMode mode={gm_Mode_Vs_States};
    exact_original_post_exit(&mode,&host.vs_sss_state);
    assert(gm_GetCurrentSceneIndex()==gmVsMode_State_Css);
    state_machine=original; vs_mode_data=vs_after_cancel;
    enter_scene(MENU_SCENE_CSS);
    error[0]=0;
    assert(!source_scene_exit(&host,MENU_SCENE_CSS,error,sizeof(error)));
    assert(strcmp(error,"CSS SSS route has no exact live VS CSS owner")==0);
    assert(gm_GetCurrentSceneIndex()==gmVsMode_State_Sss);
    assert(melee_web_vs_mode_next_state()==gmVsMode_State_Css);
    finish();
    puts("EXPECTED_PREFIX_FAILURE retained: actual SSS OnExit queues CSS, host route stays SSS, next CSS exit rejects exact-owner guard");
}
#endif
#ifndef EXPECT_PREFIX_ONLY
static void test_fixed_repeated_cancel_reentry(void) {
    initialize();
    for(int cycle=0;cycle<2;cycle++) {
        if(cycle==0) enter_scene(MENU_SCENE_CSS);
        css_to_sss();
        enter_scene(MENU_SCENE_SSS);
        cancel_current_sss();
        enter_scene(MENU_SCENE_CSS);
    }
    finish();
    puts("PASS fixed two CSS->SSS->cancel-to-CSS cycles; each host commit matches extracted gm_801A4014 post-exit route");
}
static void test_refusals(void) {
    char error[192]={0};
    initialize(); enter_scene(MENU_SCENE_CSS); css_to_sss(); enter_scene(MENU_SCENE_SSS);
    struct stateMachine before=state_machine;
    state_machine.routing.curr_state_id=gmVsMode_State_Css;
    struct stateMachine wrong_index=state_machine;
    error[0]=0;
    assert(!source_scene_exit(&host,MENU_SCENE_SSS,error,sizeof(error)));
    assert(strstr(error,"exact live route owner")!=NULL);
    assert(memcmp(&state_machine,&wrong_index,sizeof(state_machine))==0);
    state_machine=before;

    state_machine.pending_mode_change=1;state_machine.routing.pending_mode=GM_MENU;
    struct stateMachine pending_mode=state_machine;
    error[0]=0;
    assert(!source_scene_exit(&host,MENU_SCENE_SSS,error,sizeof(error)));
    assert(strstr(error,"exact live route owner")!=NULL);
    assert(memcmp(&state_machine,&pending_mode,sizeof(state_machine))==0);
    state_machine=before;

    state_machine.routing.next_state_id=(u8)(gmVsMode_State_Vs+1);
    struct stateMachine pending_state=state_machine;
    error[0]=0;
    assert(!source_scene_exit(&host,MENU_SCENE_SSS,error,sizeof(error)));
    assert(strstr(error,"exact live route owner")!=NULL);
    assert(memcmp(&state_machine,&pending_state,sizeof(state_machine))==0);
    state_machine=before;
    finish();
}
static void test_bad_commit_queue(int target) {
    char error[192]={0};
    initialize(); enter_scene(MENU_SCENE_CSS); css_to_sss(); enter_scene(MENU_SCENE_SSS);
    struct stateMachine before=state_machine;
    session.sss.start_game=0;
    host.vs_sss_state.info.exit_data=&session.sss;
    gm_Mode_Vs_States[gmVsMode_State_Sss].on_exit(&host.vs_sss_state);
    assert(melee_web_vs_mode_next_state()==gmVsMode_State_Css);
    if(target < 0) state_machine.routing.next_state_id=0;
    else state_machine.routing.next_state_id=(u8)(target+1); /* injected callback-result fault */
    struct stateMachine after_callback=state_machine;
    assert(!host_commit_vs_sss_cancel_css_route(&host,&session.sss,error,sizeof(error)));
    assert(strstr(error,"did not queue its authored CSS route")!=NULL);
    assert(memcmp(&state_machine,&after_callback,sizeof(state_machine))==0);
    state_machine=before;
    finish();
}
static void test_bad_post_callback_route_refused(void) {
    test_bad_commit_queue(gmVsMode_State_Vs);
    test_bad_commit_queue(-1);
}
static void test_start_game_stays_outside_cancel_route(void) {
    char error[192]={0};
    initialize(); enter_scene(MENU_SCENE_CSS); css_to_sss(); enter_scene(MENU_SCENE_SSS);
    session.sss.start_game=1;
    assert(source_scene_exit(&host,MENU_SCENE_SSS,error,sizeof(error)));
    assert(!host.vs_mode_owned);
    assert(!melee_web_vs_mode_owned);
    assert(memcmp(&state_machine,&caller_snapshot,sizeof(state_machine))==0);
    puts("PASS start_game handoff remains outside cancel route helper");
}
#endif
int main(void) {
#ifdef EXPECT_PREFIX_ONLY
    test_expected_prefix_failure();
#else
    test_fixed_repeated_cancel_reentry();
    test_refusals();
    test_bad_post_callback_route_refused();
    test_start_game_stays_outside_cancel_route();
#endif
    return 0;
}
'''


class SssCancelReentryTests(OwnedWorkspaceTests):
    def test_actual_source_cancel_reentry(self):
        if not all(path.is_file() for path in [GM, MODE, MODE_H, FORWARD, VS, PATCH]):
            self.skipTest("pinned .deps/melee source is unavailable in this test shard")
        gm = GM.read_text(encoding="utf-8")
        mode = MODE.read_text(encoding="utf-8")
        mode_h = MODE_H.read_text(encoding="utf-8")
        forward = FORWARD.read_text(encoding="utf-8")
        vs = VS.read_text(encoding="utf-8")
        patch = PATCH.read_text(encoding="utf-8")
        candidate_host = HOST.read_text(encoding="utf-8")
        if BASELINE_HOST is not None and not BASELINE_HOST.is_file():
            self.fail(f"explicit pre-fix source file is missing: {BASELINE_HOST}")
        baseline_host = BASELINE_HOST.read_text(encoding="utf-8") if BASELINE_HOST is not None else None
        scene_css = scene_enum_value(forward, "GS_CSS")
        scene_sss = scene_enum_value(forward, "GS_SSS")
        rows = get_authored_rows(mode)
        table = []
        for row in rows:
            if row == "GM_GAMEMODESTATE_TERMINATE":
                table.append("{.id=GM_GAMEMODESTATE_TERMINATE}")
            elif row == "gmVsMode_State_Css":
                table.append("{.id=gmVsMode_State_Css,.info={.scene_kind=GS_CSS},.on_enter=fixture_on_enter,.on_exit=onExitCss}")
            elif row == "gmVsMode_State_Sss":
                table.append("{.id=gmVsMode_State_Sss,.info={.scene_kind=GS_SSS},.on_enter=fixture_on_enter,.on_exit=onExitSss}")
            else:
                table.append(f"{{.id={row}}}")
        patch_added = "\n".join(line[1:] for line in patch.splitlines() if line.startswith("+") and not line.startswith("+++"))
        common = [
            function(gm, "static inline u8 firstState("),
            function(gm, "static inline u8 nextState("),
            function(gm, "u8 gm_GetCurrentGameMode(void)"),
            function(gm, "u8 gm_GetCurrentSceneIndex(void)"),
            function(gm, "u8 gm_GetPreviousSceneIndex(void)"),
            function(gm, "void gm_ChangeGameModeAfterCurrentScene(int pending_mode)"),
            function(gm, "void gm_SetNextGameModeStateId(u8 curr_id)"),
            function(patch_added, "int melee_web_vs_mode_begin(void)"),
            function(patch_added, "int melee_web_vs_mode_end(void)"),
            function(patch_added, "int melee_web_vs_mode_select_state(int id)"),
            function(patch_added, "int melee_web_vs_mode_next_state(void)"),
            function(patch_added, "int melee_web_vs_mode_resolve_next_state(GameModeState* states)"),
            function(patch_added, "int melee_web_vs_mode_pending_mode(void)"),
            function(patch_added, "int melee_web_vs_mode_set_route(int current_mode, int previous_mode)"),
            function(vs, "void gmVsMelee_ExitCss(GameModeState* state, VsModeData* vs)"),
            function(vs, "void gmVsMelee_ExitSss("),
            function(mode, "void onExitCss(GameModeState* state)"),
            function(mode, "void onExitSss(GameModeState* state)"),
        ]
        state_vars = patch_added[patch_added.index("static struct stateMachine melee_web_saved_vs_mode;"):patch_added.index("int melee_web_vs_mode_begin(void)")]
        scratch = self.new_workspace(ROOT, "sss-cancel-reentry-")
        work_root = EVIDENCE if EVIDENCE is not None else scratch
        work_root.mkdir(parents=True, exist_ok=True)
        source_bindings = {
            "source_head": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
            "source_tree": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD^{tree}"], text=True).strip(),
            "baseline_host_source": str(BASELINE_HOST) if BASELINE_HOST is not None else None,
            "baseline_host_sha256": hashlib.sha256(baseline_host.encode()).hexdigest() if baseline_host is not None else None,
            "candidate_host_sha256": hashlib.sha256(candidate_host.encode()).hexdigest(),
            "files": {},
        }
        for path in [GM, MODE, MODE_H, FORWARD, VS, PATCH, ROOT / "tests/test_first_css_final_pending_draw_host.py"]:
            source_bindings["files"][str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
        (work_root / "source-bindings.json").write_text(json.dumps(source_bindings, indent=2) + "\n")

        def make_control(host_source, name, define):
            host_head = [
                function(host_source, "static int fail(char* e,size_t n,const char* text)"),
                function(host_source, "static int ok(char* e,size_t n)"),
                function(host_source, "static int live(MeleeWebMenuHost* h,char* e,size_t n)"),
                function(host_source, "static GameModeState* training_state_for_scene("),
                function(host_source, "static int host_commit_vs_css_sss_route("),
            ]
            cancel_helpers = []
            if "static int host_prepare_vs_sss_cancel_css_route(" in host_source:
                cancel_helpers = [
                    function(host_source, "static int host_prepare_vs_sss_cancel_css_route("),
                    function(host_source, "static int host_commit_vs_sss_cancel_css_route("),
                ]
            host_tail = [
                function(host_source, "static int source_scene_enter("),
                function(host_source, "static int source_scene_exit("),
            ]
            source = TEMPLATE.replace("@GS_CSS@", str(scene_css)).replace("@GS_SSS@", str(scene_sss))
            source = source.replace("@ENUM@", get_enum(mode_h)).replace("@MACHINE@", get_machine(gm))
            source = source.replace("@VS_LEASE_STATE@", state_vars).replace("@ROWS@", ",".join(table))
            source = source.replace("@ORIGINAL_AND_HOST_FUNCTIONS@", "\n".join(common + host_head + cancel_helpers + host_tail))
            source = source.replace("@POST_EXIT@", get_post_exit(gm))
            if define:
                source = "#define EXPECT_PREFIX_ONLY 1\n" + source
            c_path = work_root / f"{name}.c"
            exe_path = work_root / name
            c_path.write_text(source, encoding="utf-8")
            command = shlex.split(os.environ.get("CC", "cc")) + ["-std=c11", "-Wall", "-Wextra", "-Werror", str(c_path), "-o", str(exe_path)]
            compiled = subprocess.run(command, cwd=work_root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (work_root / f"{name}-compile.json").write_text(json.dumps({"argv": command, "cwd": str(work_root), "returncode": compiled.returncode}, indent=2) + "\n")
            (work_root / f"{name}-compile.log").write_text(compiled.stdout, encoding="utf-8")
            self.assertEqual(compiled.returncode, 0, compiled.stdout)
            ran = subprocess.run([str(exe_path)], cwd=work_root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            (work_root / f"{name}-run.json").write_text(json.dumps({"argv": [str(exe_path)], "cwd": str(work_root), "returncode": ran.returncode}, indent=2) + "\n")
            (work_root / f"{name}-run.log").write_text(ran.stdout, encoding="utf-8")
            self.assertEqual(ran.returncode, 0, ran.stdout)
            return ran.stdout

        if baseline_host is not None:
            before = make_control(baseline_host, "prefix-control", True)
            self.assertIn("EXPECTED_PREFIX_FAILURE retained", before)
        fixed = make_control(candidate_host, "fixed-control", False)
        self.assertIn("PASS fixed two CSS->SSS->cancel-to-CSS cycles", fixed)
        self.assertIn("PASS start_game handoff remains outside cancel route helper", fixed)


if __name__ == "__main__":
    unittest.main(verbosity=2)
