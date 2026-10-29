"""Extracted-source component controls for two authored Stadium display routes.

The test compiles selected production function bodies against explicit modeled
player, text, stage, and RNG service leaves. It establishes component branch
behavior only; it is not an original trace, browser/native capture, long idle
lifetime, or C3 per-owner parity result.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path(os.environ.get("MELEE_WEB_CANDIDATE_SOURCE_ROOT", ROOT)).resolve()
sys.path.insert(0, str(SOURCE_ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


def _function(source: str, signature: str) -> str:
    if source.count(signature) != 1:
        raise AssertionError(f"expected one production function: {signature}")
    start = source.index(signature)
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing production function body: {signature}")
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
                state = "line_comment"
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
        elif state == "line_comment" and char == "\n":
            state = "code"
        index += 1
    raise AssertionError(f"unterminated production function: {signature}")


PRELUDE = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef int8_t s8; typedef int16_t s16; typedef int32_t s32;
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
typedef float f32;
#define UNUSED __attribute__((unused))
#define PAD_STACK(n) ((void)(n))
#define HSD_ASSERT(line, expression) assert(expression)
#define GET_GROUND(g) ((Ground *)((g)->user_data))
#define GET_WRAPPER(p) ((ImageDescWrapper *)(p))

enum { Gr_Kind_PStadium=3, PsType_Display=2,
       Gm_PKind_NA=0, Gm_PKind_Human=1, Gm_PKind_Cpu=2,
       CmSubjectState_Inactive=0, CmSubjectState_Active=1 };
typedef struct { f32 x, y; } Vec2;
typedef struct { u8 r, g, b, a; } GXColor;
typedef struct HSD_Text {
    int default_kerning, default_alignment, default_fitting;
    GXColor text_color;
    Vec2 x34;
} HSD_Text;
typedef struct HSD_GObj { void *user_data; } HSD_GObj;
typedef HSD_GObj Ground_GObj;
typedef struct { HSD_Text *win_static_p, *win_dynamic_p; } TextWrapper;
typedef struct { int state; } CmSubject;
typedef struct { int flag; } ImageDescWrapper;
typedef struct { u8 x, y, z, w; } U8Vec4;
typedef union StadiumColor { GXColor color; U8Vec4 vector; u32 packed; } StadiumColor;
typedef struct StadiumAlphaPresets { U8Vec4 entries[4]; } StadiumAlphaPresets;
const StadiumAlphaPresets grPs_803B7F8C = {{
    {0,0,0,0x50}, {0,0,0,0x40}, {0,0,0,0x30}, {0,0,0,0x20}
}};
const GXColor grPs_804DAF70 = {0xFF,0xFF,0xFF,0xFF};
typedef struct {
    int x20, x24, x28, x2C, x30, x34, x38, x3C;
    int x48, x4A, x4C, x4E, x50;
} YakumonoParam;
typedef struct {
    int xE0, xE4, xE6, xEA, xEE, xF0, xF2;
    HSD_GObj *xD4;
    void *xD8, *xDC;
    CmSubject *xF4;
    unsigned xF8_0:1, xF8_1:1;
} DisplayFields;
typedef struct Ground { struct { DisplayFields display; } u; } Ground;
typedef struct { int grkind; } StageInfo;
struct HSD_GObj;
typedef struct { int x1, slot_type; } FighterMatchInfo;
typedef struct {
    u8 unk_D;
    struct { int is_stock, match_kind, x2_5; } x24C8;
    FighterMatchInfo FighterMatchInfo[6];
} lbl_8046B6A0_t;
typedef struct StartMeleeRules { int x2_7; } StartMeleeRules;
typedef struct { int explode_animation, randomize_velocity; } IfDamageFlags;
typedef struct { IfDamageFlags flags; int unk9; } IfDamageState;
typedef struct { IfDamageState players[6]; } IfStatusHud;

static StageInfo stage_info;
static YakumonoParam *yakumono_param;
static HSD_GObj display_gobj, text_gobj, player_gobjs[6];
static Ground display_ground;
static HSD_Text static_text, dynamic_text;
static TextWrapper text_wrapper;
static int map_available, ground_lookup_calls;
static int slot_types[6], stocks[6], player_defeated[6], player_teams[6];
static HSD_GObj *player_entities[6];
static float random_values[8];
static int random_value_count, random_calls;
static int sis_previous_static, sis_last_static, sis_static_calls, sis_clear_calls, sis_dynamic_calls;
static float sis_last_x, sis_last_y;
static char sis_last_format[128];
static int sis_last_integer;
static char sis_last_string[128];
static lbl_8046B6A0_t match_data;
static StartMeleeRules melee_rules;
static IfStatusHud hud_data;

static HSD_GObj *Ground_GetMapGObj(int map_id);
void grStadium_801D3A0C(HSD_GObj *gobj);
void grStadium_801D3BBC(HSD_GObj *gobj);
static void grStadium_801D3F40(HSD_GObj *gobj, int mode);
static void grStadium_801D4194(HSD_GObj *gobj);
static void grStadium_801D39A0(HSD_GObj *gobj);
static int grStadium_801D32D0(HSD_GObj *gobj);
void grStadium_801D2528(HSD_GObj *gobj, int mode, int timer);
void grStadium_801D2A60(HSD_GObj *gobj);
void grStadium_801D2344(HSD_GObj *gobj);
void grStadium_801D3B4C(int player, int slot_type);
void gm_8016B8D4(s32 player, u8 slot_type);
static void ifStatus_TriggerStockLoss(s32 player_idx, void (*callback)(s32));

static HSD_GObj *Ground_GetMapGObj(int map_id)
{ assert(map_id == PsType_Display); ++ground_lookup_calls; return map_available ? &display_gobj : NULL; }
static void *HSD_GObjGetUserData(HSD_GObj *gobj) { return gobj->user_data; }
static int gm_8018841C(void) { return 0; }
static int randi_between_2(int a, int b) { (void)a; (void)b; return 1; }
static int randi_between(int a, int b) { (void)a; (void)b; return 1; }
static float HSD_Randf(void)
{ assert(random_calls < random_value_count); return random_values[random_calls++]; }
static int Player_GetPlayerSlotType(int player) { return slot_types[player]; }
static int Player_GetStocks(int player) { return stocks[player]; }
static int Player_8003219C(int player) { return player_defeated[player]; }
static HSD_GObj *Player_GetEntity(int player) { return player_entities[player]; }
static int Player_GetTeam(int player) { return player_teams[player]; }
static int gm_8016C6C0(HSD_GObj *gobj) { (void)gobj; return 0; }
static int lbLang_IsSavedLanguageJP(void) { return 0; }
static int gm_8016B168(void) { return 0; }
static int gm_80160854(int player, int team, int arg, int slot)
{ (void)player; (void)team; (void)arg; (void)slot; return 0; }
static GXColor gm_80160968(int value) { (void)value; return (GXColor){0x10,0x20,0x30,0xFF}; }
static const char *gm_80160A60(int player) { static const char *names[6]={"P1","P2","P3","P4","P5","P6"}; return names[player]; }
static void HSD_SisLib_803A7664(HSD_Text *text) { assert(text); ++sis_clear_calls; }
static void HSD_SisLib_803A6368(HSD_Text *text, int entry)
{ assert(text); sis_previous_static=sis_last_static; sis_last_static=entry; ++sis_static_calls; }
static void HSD_SisLib_803A6B98(HSD_Text *text, float x, float y, const char *format, ...)
{
    va_list args; assert(text); ++sis_dynamic_calls; sis_last_x=x; sis_last_y=y;
    snprintf(sis_last_format,sizeof(sis_last_format),"%s",format);
    va_start(args,format);
    if (strstr(format,"%d") != NULL) sis_last_integer=va_arg(args,int);
    if (strstr(format,"%s") != NULL) {
        const char *value=va_arg(args,const char *);
        snprintf(sis_last_string,sizeof(sis_last_string),"%s",value);
    }
    va_end(args);
}
static lbl_8046B6A0_t *gm_16AE_GetUnkData_0(void) { return &match_data; }
static StartMeleeRules *gm_GetRules(void) { return &melee_rules; }
static IfStatusHud *ifStatus_GetHUDInfo(void) { return &hud_data; }
static int grStadium_801D32D0(HSD_GObj *gobj) { (void)gobj; return 1; }
static void grStadium_801D4194(HSD_GObj *gobj) { (void)gobj; }
static void grStadium_801D39A0(HSD_GObj *gobj) { (void)gobj; }
static void grStadium_801D3F40(HSD_GObj *gobj, int mode) { (void)gobj; (void)mode; }
'''


def _production_translation_unit() -> str:
    base = SOURCE_ROOT / "build/gameplay-source/src/melee"
    stadium_path = base / "gr/grpstadium.c"
    gm_path = base / "gm/gm_16AE.c"
    if_path = base / "if/ifstatus.c"
    stadium = stadium_path.read_text(encoding="utf-8")
    gm = gm_path.read_text(encoding="utf-8")
    ifstatus = if_path.read_text(encoding="utf-8")
    definitions = [
        _function(stadium, "void grStadium_801D3A0C("),
        _function(stadium, "static inline u8 grStadium_ScaleColor("),
        _function(stadium, "void grStadium_801D3BBC("),
        _function(stadium, "void grStadium_801D2528("),
        _function(stadium, "void grStadium_801D2A60("),
        _function(stadium, "void grStadium_801D3B4C("),
        _function(gm, "void gm_8016B8D4("),
        _function(stadium, "void grStadium_801D2344("),
        _function(ifstatus, "static inline void ifStatus_TriggerStockLoss("),
    ]
    return PRELUDE + "\n" + "\n".join(definitions) + "\n" + C_FIXTURE


C_FIXTURE = r'''
static void fixture_reset(void)
{
    int i;
    memset(&stage_info,0,sizeof(stage_info));
    memset(&display_gobj,0,sizeof(display_gobj));
    memset(&text_gobj,0,sizeof(text_gobj));
    memset(&display_ground,0,sizeof(display_ground));
    memset(&static_text,0,sizeof(static_text));
    memset(&dynamic_text,0,sizeof(dynamic_text));
    memset(&text_wrapper,0,sizeof(text_wrapper));
    memset(&match_data,0,sizeof(match_data));
    memset(&melee_rules,0,sizeof(melee_rules));
    memset(&hud_data,0,sizeof(hud_data));
    memset(slot_types,0,sizeof(slot_types)); memset(stocks,0,sizeof(stocks));
    memset(player_defeated,0,sizeof(player_defeated)); memset(player_teams,0,sizeof(player_teams));
    memset(player_entities,0,sizeof(player_entities)); memset(random_values,0,sizeof(random_values));
    stage_info.grkind=Gr_Kind_PStadium; map_available=1; ground_lookup_calls=0;
    display_gobj.user_data=&display_ground; text_gobj.user_data=&text_wrapper;
    display_ground.u.display.xD4=&text_gobj;
    text_wrapper.win_static_p=&static_text; text_wrapper.win_dynamic_p=&dynamic_text;
    yakumono_param=NULL;
    random_value_count=0; random_calls=0; sis_last_static=-1; sis_static_calls=0;
    sis_clear_calls=0; sis_dynamic_calls=0; sis_last_x=0; sis_last_y=0;
    sis_last_format[0]=0; sis_last_integer=-1; sis_last_string[0]=0;
    for (i=0;i<6;i++) { slot_types[i]=Gm_PKind_NA; player_entities[i]=NULL; }
}
static void configure_two_active_players(void)
{
    slot_types[0]=Gm_PKind_Human; slot_types[1]=Gm_PKind_Cpu;
    player_entities[0]=&player_gobjs[0]; player_entities[1]=&player_gobjs[1];
}
static void test_zero_stock_mode9(void)
{
    YakumonoParam params={.x20=90,.x24=240,.x2C=300,.x48=50,.x4A=20,.x4C=20,.x4E=10,.x50=7};
    fixture_reset(); yakumono_param=&params; configure_two_active_players();
    match_data.x24C8.is_stock=1; match_data.x24C8.match_kind=0;
    slot_types[2]=Gm_PKind_Human; stocks[2]=0;
    ifStatus_TriggerStockLoss(2,NULL);
    assert(match_data.FighterMatchInfo[2].x1==0x82);
    assert(match_data.FighterMatchInfo[2].slot_type==Gm_PKind_Human);
    assert(ground_lookup_calls==1);
    assert(display_ground.u.display.xEE==2 && display_ground.u.display.xF0==Gm_PKind_Human);
    assert(display_ground.u.display.xEA==0 && display_ground.u.display.xE4==9);
    assert(display_ground.u.display.xE0==240 && display_ground.u.display.xE6==0x40);
    assert(display_ground.u.display.xF8_0==0 && display_ground.u.display.xF8_1==0);
    assert(sis_last_static==3 && sis_static_calls==1 && sis_clear_calls==1);
    assert(sis_dynamic_calls==1 && sis_last_x==125.0f && sis_last_y==56.0f);
    assert(strcmp(sis_last_format,"Ｐｌａｙｅｒ %d Ｄｅｆｅａｔｅｄ")==0);
    assert(sis_last_integer==3);

    /* Neighbor control: a player with stocks remaining is not a stock-out. */
    fixture_reset(); yakumono_param=&params; match_data.x24C8.is_stock=1;
    slot_types[2]=Gm_PKind_Human; stocks[2]=1;
    ifStatus_TriggerStockLoss(2,NULL);
    assert(ground_lookup_calls==0 && display_ground.u.display.xE4==0);
    assert(match_data.FighterMatchInfo[2].x1==0);

    /* Actual source fallback: non-Stadium rules update match info but do not
       request the Stadium display object or select its mode-9 screen. */
    fixture_reset(); yakumono_param=&params; stage_info.grkind=99;
    match_data.x24C8.is_stock=1; slot_types[2]=Gm_PKind_Human; stocks[2]=0;
    ifStatus_TriggerStockLoss(2,NULL);
    assert(match_data.FighterMatchInfo[2].x1==0x82);
    assert(ground_lookup_calls==0 && display_ground.u.display.xE4==0);
}
static void test_picker_threshold_timer_and_sis(void)
{
    YakumonoParam params={.x20=90,.x24=240,.x2C=300,
        .x48=50,.x4A=20,.x4C=20,.x4E=10,.x50=7};
    int i, sis_before_mode14;
    fixture_reset(); yakumono_param=&params; configure_two_active_players();
    display_ground.u.display.xE4=8; display_ground.u.display.xEA=7;
    display_ground.u.display.xF2=6;
    random_values[0]=0.1f; random_values[1]=0.8f; random_value_count=2;
    grStadium_801D2A60(&display_gobj);
    assert(random_calls==2 && display_ground.u.display.xE4==1);
    assert(display_ground.u.display.xEA==8 && display_ground.u.display.xF2==7);
    assert(display_ground.u.display.xE0==90);

    /* The next actual selector call reaches its authored seven-pick branch:
       it does not ask HSD_Randf, resets/increments xF2, and initializes the
       mode-14 timer plus the two-active-player SIS table selection. */
    sis_before_mode14=sis_static_calls;
    grStadium_801D2A60(&display_gobj);
    assert(random_calls==2 && display_ground.u.display.xE4==14);
    assert(display_ground.u.display.xEA==1 && display_ground.u.display.xF2==1);
    assert(display_ground.u.display.xE0==300 && display_ground.u.display.xE6==0x40);
    /* The mode-1 renderer is a modeled leaf here; assert the actual mode-14
       selector call as a delta, not a whole-history SIS total. */
    assert(sis_last_static==0xF && sis_static_calls-sis_before_mode14==1);

    /* Re-enter the real stock-out mode, then follow the production update
       countdown through its post-decrement boundary into that same picker. */
    fixture_reset(); yakumono_param=&params; configure_two_active_players();
    match_data.x24C8.is_stock=1; slot_types[2]=Gm_PKind_Human; stocks[2]=0;
    ifStatus_TriggerStockLoss(2,NULL);
    display_ground.u.display.xF2=7; /* explicit component branch input */
    for (i=0;i<241;i++) grStadium_801D2344(&display_gobj);
    assert(display_ground.u.display.xE4==9 && display_ground.u.display.xE0==-1);
    assert(sis_last_static==3);
    sis_before_mode14=sis_static_calls;
    grStadium_801D2344(&display_gobj);
    assert(display_ground.u.display.xE4==14 && display_ground.u.display.xEA==9);
    assert(display_ground.u.display.xF2==1 && display_ground.u.display.xE0==300);
    assert(random_calls==0 && sis_last_static==0xF);
    /* The actual update redraws mode9, then the picker initializes mode14. */
    assert(sis_static_calls-sis_before_mode14==2);
    assert(sis_previous_static==3 && sis_last_static==0xF);
}
int main(int argc, char **argv)
{
    assert(argc==2);
    if (strcmp(argv[1],"stockout")==0) test_zero_stock_mode9();
    else if (strcmp(argv[1],"picker")==0) test_picker_threshold_timer_and_sis();
    else return 2;
    puts("COMPONENT_ONLY_MODELED_LEAVES: PASS");
    return 0;
}
'''


class StadiumC3DisplayBranchTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(SOURCE_ROOT, "stadium-c3-display-branches-")
        compiler = shutil.which(os.environ.get("CC", "clang")) or shutil.which("cc")
        if not compiler:
            raise RuntimeError("clang or cc is required for extracted-source controls")
        cls.binary = cls.workspace / "stadium-c3-display-branches"
        source_file = cls.workspace / "stadium_c3_display_branches.c"
        source_file.write_text(_production_translation_unit(), encoding="utf-8")
        command = [compiler, "-std=c11", "-g", "-O0", "-fsanitize=address,undefined",
                   "-fno-omit-frame-pointer", "-Wno-unused-parameter", "-Wno-unused-variable",
                   "-Werror=implicit-function-declaration", str(source_file), "-o", str(cls.binary)]
        result = subprocess.run(command, cwd=SOURCE_ROOT, capture_output=True, text=True,
                                timeout=60, check=False)
        if result.returncode:
            raise AssertionError(f"extracted Stadium branch compile failed\n{result.stdout}\n{result.stderr}")

    def _run_case(self, case: str):
        result = subprocess.run([str(self.binary), case], cwd=SOURCE_ROOT,
                                capture_output=True, text=True, timeout=10, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("COMPONENT_ONLY_MODELED_LEAVES: PASS", result.stdout)

    def test_actual_zero_stock_mode9_and_neighbors(self):
        self._run_case("stockout")

    def test_actual_picker_threshold_mode14_timer_and_sis(self):
        self._run_case("picker")


if __name__ == "__main__":
    unittest.main()
