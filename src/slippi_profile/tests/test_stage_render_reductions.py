# SPDX-License-Identifier: GPL-3.0-or-later
"""Pinned actual-source reductions for separate Whispy and Stadium hooks."""

import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
MELEE = ROOT / ".deps/melee"
BASE_PATCH = ROOT / "patches/melee-gameplay.patch"
PROFILE_PATCH = ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"
WHISPY_SOURCE = MELEE / "src/melee/ft/ftlib.c"
EXPECTED_MELEE_COMMIT = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
EXPECTED_WHISPY_SOURCE_SHA256 = "78bc9c9a87439b1db063bf033b8f18869d43105917308f6efcf08d83fc7b6693"
WHISPY_PROFILE_GUARD = """#ifdef MELEE_WEB_SLIPPI_PROFILE
            if (!slippi_profile_whispy_zero_direction_contribution(
                    cur_fp->motion_id))
            {
                result += sgn(vec.x - v->x);
            }
#else
            result += sgn(vec.x - v->x);
#endif"""


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _matching_brace(source, opening):
    if source[opening] != "{":
        raise AssertionError("function extraction did not start at an opening brace")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if char == "*" and following == "/":
                state = "code"
                index += 1
        elif state in {"string", "character"}:
            quote = '"' if state == "string" else "'"
            if char == "\\":
                index += 1
            elif char == quote:
                state = "code"
        elif char == "/" and following == "/":
            state = "line"
            index += 1
        elif char == "/" and following == "*":
            state = "block"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "character"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise AssertionError("unterminated source function")


def _function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError(f"expected one pinned source function {signature!r}")
    start = source.index(signature)
    opening = source.index("{", start)
    return source[start:_matching_brace(source, opening)] + "\n"


def _run(command, cwd, **kwargs):
    return subprocess.run(command, cwd=cwd, check=False, timeout=90, **kwargs)


def _materialize_whispy(output):
    source_repo = output / "source-reduction"
    target = "src/melee/ft/ftlib.c"
    source_path = source_repo / target
    source_path.parent.mkdir(parents=True)
    original_bytes = WHISPY_SOURCE.read_bytes()
    if _sha(original_bytes) != EXPECTED_WHISPY_SOURCE_SHA256:
        raise AssertionError("pinned ftlib.c bytes changed")
    commit = subprocess.check_output(
        ["git", "-C", str(MELEE), "rev-parse", "HEAD"], cwd=ROOT, text=True, timeout=30,
    ).strip()
    if commit != EXPECTED_MELEE_COMMIT:
        raise AssertionError(f"unexpected Melee source commit {commit}")
    source_path.write_bytes(original_bytes)
    subprocess.run(["git", "-C", str(source_repo), "init", "-q"], check=True, timeout=30)
    subprocess.run(
        ["git", "-C", str(source_repo), "apply", "--check",
         f"--include={target}", str(BASE_PATCH)], cwd=ROOT, check=True, timeout=30,
    )
    subprocess.run(
        ["git", "-C", str(source_repo), "apply", f"--include={target}",
         str(BASE_PATCH)], cwd=ROOT, check=True, timeout=30,
    )

    # Apply the repository's existing boolean-token migration to this exact
    # source file, using its production transform rather than a copied rule.
    import sys
    sys.path.insert(0, str(ROOT / "scripts"))
    import gameplay_bool
    transformed = gameplay_bool.transform_source(
        Path(target), source_path.read_text(encoding="utf-8"),
    )
    source_path.write_text(transformed, encoding="utf-8")
    # Other reviewed hooks also edit ftlib.c. Keep whole-file digests as
    # provenance below; gate the original and profile Whispy function itself.
    base_composed_bytes = source_path.read_bytes()
    base_composed_source = base_composed_bytes.decode("utf-8")

    subprocess.run(
        ["git", "-C", str(source_repo), "apply", "--check",
         f"--include={target}", str(PROFILE_PATCH)], cwd=ROOT, check=True, timeout=30,
    )
    subprocess.run(
        ["git", "-C", str(source_repo), "apply", f"--include={target}",
         str(PROFILE_PATCH)], cwd=ROOT, check=True, timeout=30,
    )
    profile_bytes = source_path.read_bytes()
    original_source = original_bytes.decode("utf-8")
    base_function = _function(
        base_composed_source, "float ftLib_800864A8(Vec3* v, HSD_GObj* gobj)",
    )
    profile_source = profile_bytes.decode("utf-8")
    sign = _function(original_source, "static inline s32 sgn(float x)")
    original_function = _function(
        original_source, "float ftLib_800864A8(Vec3* v, HSD_GObj* gobj)",
    )
    profile_function = _function(
        profile_source, "float ftLib_800864A8(Vec3* v, HSD_GObj* gobj)",
    )
    query = "ftLib_800866DC(cur, &vec);"
    state_read = "cur_fp->motion_id"
    contribution = "result += sgn(vec.x - v->x);"
    if base_function != original_function:
        raise AssertionError("Whispy target function changed in the base gameplay overlay")
    if original_function.count(contribution) != 1:
        raise AssertionError("Whispy original contribution source count changed")
    contribution_line = "            " + contribution
    expected_profile_function = original_function.replace(
        contribution_line, WHISPY_PROFILE_GUARD, 1,
    )
    if profile_function != expected_profile_function:
        raise AssertionError("Whispy target function differs from the reviewed profile hook")
    if profile_function.index(query) >= profile_function.index(state_read):
        raise AssertionError("Whispy profile reads motion before the original bone query")
    if profile_function.index(state_read) >= profile_function.index(contribution):
        raise AssertionError("Whispy profile motion gate moved after contribution")
    if profile_function.count(query) != 1 or profile_function.count("HSD_Randi(2)") != 1:
        raise AssertionError("Whispy query/fallback source call count changed")
    if "ftLib_800866DC(cur, &vec);\n#ifdef MELEE_WEB_SLIPPI_PROFILE" not in profile_function:
        raise AssertionError("Whispy motion filter is not after the source bone query")

    omitted = profile_function.replace(WHISPY_PROFILE_GUARD, contribution_line, 1)
    if omitted == profile_function:
        raise AssertionError("could not remove exactly the Whispy profile contribution hook")

    (output / "source_identity.json").write_text(
        __import__("json").dumps({
            "melee_commit": commit,
            "pinned_ftlib_sha256": _sha(original_bytes),
            "base_composed_ftlib_sha256": _sha(base_composed_bytes),
            "profile_ftlib_sha256": _sha(profile_bytes),
            "sign_function_sha256": _sha(sign.encode()),
            "original_function_sha256": _sha(original_function.encode()),
            "profile_function_sha256": _sha(profile_function.encode()),
            "omitted_hook_function_sha256": _sha(omitted.encode()),
            "source_patch_sha256": _sha(PROFILE_PATCH.read_bytes()),
            "whispy_comparison_scope": (
                "Exact ftLib_800864A8 function bytes against the pinned original "
                "and reviewed profile transform; whole-file digests are provenance only."
            ),
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    self_filter = _function(original_source, "bool ftLib_80086FD4(HSD_GObj* gobj0, HSD_GObj* gobj1)")
    self_filter = gameplay_bool.transform_source(Path(target), self_filter)
    return sign + "\n" + self_filter, original_function, profile_function, omitted


def _fixture(sign, original_function, profile_function, omitted_function):
    preamble = r'''/*
 * Actual ftLib_800864A8 and sgn source are extracted from pinned doldecomp/melee b43912cc.
 * Fighter lists and camera-bone results are synthetic; unknown calls fail link.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "slippi_profile/whispy_direction_fix.h"

typedef uint8_t u8;
typedef int32_t s32;
typedef int melee_source_bool; /* Production gameplay_bool ABI. Vec3 from gameplay_compat.h. */
typedef struct Fighter {
    int player_id;
    int x221F_b3;
    int team;
    int32_t motion_id;
    float synthetic_bone_x;
    int fixture_id;
} Fighter;
typedef struct HSD_GObj {
    struct HSD_GObj *next;
    void *user_data;
} HSD_GObj;
typedef struct FighterList { HSD_GObj *fighters; } FighterList;
static FighterList fixture_fighter_list;
#define HSD_GObj_Entities (&fixture_fighter_list)
static int fixture_team_mode;
static int fixture_rng_value;
static unsigned fixture_rng_calls;
static unsigned fixture_query_calls;
static int fixture_query_order[8];
static unsigned fixture_query_order_count;
static int fixture_motion_after_query = -99;

#define GET_FIGHTER(gobj) ((Fighter*) (gobj)->user_data)
static int gm_8016B168(void) { return fixture_team_mode; }
static void ftLib_800866DC(HSD_GObj *gobj, Vec3 *position)
{
    Fighter *fighter = gobj->user_data;
    assert(fixture_query_order_count < 8);
    fixture_query_order[fixture_query_order_count++] = fighter->fixture_id;
    fixture_query_calls++;
    position->x = fighter->synthetic_bone_x;
    position->y = 0.0f;
    position->z = 0.0f;
    if (fighter->fixture_id == 3 && fixture_motion_after_query != -99)
        fighter->motion_id = fixture_motion_after_query;
}
static int HSD_Randi(int upper_bound)
{
    assert(upper_bound == 2 && (fixture_rng_value == 0 || fixture_rng_value == 1));
    fixture_rng_calls++;
    return fixture_rng_value;
}
static void fixture_reset_counts(void)
{
    fixture_rng_calls = 0;
    fixture_query_calls = 0;
    fixture_query_order_count = 0;
}

'''
    # The source reserves this local solely for its original stack layout. Mark
    # it used in this reduced host translation unit without changing behavior.
    sign = sign.replace("    u8 _[8];", "    u8 _[8];\n    (void) _;", 1)
    funcs = [
        sign,
        original_function.replace(
            "float ftLib_800864A8(", "float original_ftLib_800864A8(", 1,
        ).replace("    u8 _[12];", "    u8 _[12];\n    (void) _;", 1),
        profile_function.replace(
            "float ftLib_800864A8(", "float profile_ftLib_800864A8(", 1,
        ).replace("    u8 _[12];", "    u8 _[12];\n    (void) _;", 1),
        omitted_function.replace(
            "float ftLib_800864A8(", "float omitted_ftLib_800864A8(", 1,
        ).replace("    u8 _[12];", "    u8 _[12];\n    (void) _;", 1),
    ]
    main = r'''
static Fighter fixture_fighters[4];
static HSD_GObj fixture_gobjs[4];
static HSD_GObj *fixture_owner;
static Vec3 fixture_origin;
static void setup_fighters(float special_bone_x, int special_motion)
{
    for (unsigned i = 0; i < 4; ++i) {
        fixture_fighters[i] = (Fighter){0};
        fixture_gobjs[i] = (HSD_GObj){0};
        fixture_gobjs[i].user_data = &fixture_fighters[i];
        fixture_fighters[i].fixture_id = (int) i;
        fixture_fighters[i].player_id = (int) i;
    }
    fixture_gobjs[0].next = &fixture_gobjs[1];
    fixture_gobjs[1].next = &fixture_gobjs[2];
    fixture_gobjs[2].next = &fixture_gobjs[3];
    fixture_fighter_list.fighters = &fixture_gobjs[0];
    fixture_owner = &fixture_gobjs[0];
    fixture_fighters[1].synthetic_bone_x = 1.0f;
    fixture_fighters[1].motion_id = 12;
    fixture_fighters[2].synthetic_bone_x = -1.0f;
    fixture_fighters[2].motion_id = 12;
    fixture_fighters[3].synthetic_bone_x = special_bone_x;
    fixture_fighters[3].motion_id = special_motion;
    fixture_rng_value = 1;
    fixture_team_mode = 0;
    fixture_motion_after_query = -99;
    fixture_origin = (Vec3){0.0f, 0.0f, 0.0f};
    fixture_reset_counts();
}
static void assert_queries_in_source_order(void)
{
    assert(fixture_query_calls == 3);
    assert(fixture_query_order_count == 3);
    assert(fixture_query_order[0] == 1);
    assert(fixture_query_order[1] == 2);
    assert(fixture_query_order[2] == 3);
}
int main(void)
{
    setup_fighters(1.0f, 11);
    float original_drawn = original_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert_queries_in_source_order();
    assert(fixture_rng_calls == 0);
    setup_fighters(-1.0f, 11);
    float original_stale = original_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert_queries_in_source_order();
    assert(fixture_rng_calls == 0);
    assert(original_drawn == 1.0f && original_stale == -1.0f);
    assert(original_drawn != original_stale);
    puts("WHISPY_ORIGINAL_SOURCE_CAUSAL_RED");

    setup_fighters(1.0f, 11);
    float profile_drawn = profile_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert_queries_in_source_order();
    assert(fixture_rng_calls == 1);
    setup_fighters(-1.0f, 11);
    float profile_stale = profile_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert_queries_in_source_order();
    assert(fixture_rng_calls == 1);
    assert(profile_drawn == 1.0f && profile_stale == 1.0f);
    puts("WHISPY_PROFILE_GREEN_RNG_ONCE_PER_TIE");

    setup_fighters(1.0f, 11);
    float omitted_drawn = omitted_ftLib_800864A8(&fixture_origin, fixture_owner);
    setup_fighters(-1.0f, 11);
    float omitted_stale = omitted_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert(omitted_drawn == 1.0f && omitted_stale == -1.0f);
    assert(omitted_drawn != omitted_stale);
    puts("WHISPY_OMITTED_HOOK_CAUSAL_RED");

    const int excluded_states[] = {-1, 0, 10, 11};
    for (unsigned i = 0; i < sizeof(excluded_states)/sizeof(excluded_states[0]); ++i) {
        setup_fighters(-1.0f, excluded_states[i]);
        fixture_rng_value = 0;
        assert(profile_ftLib_800864A8(&fixture_origin, fixture_owner) == -1.0f);
        assert(fixture_rng_calls == 1);
        assert_queries_in_source_order();
    }
    /* Ordering probe only: query adapter changes the observed motion before
     * the actual source hook reads it; not a gameplay state fabrication. */
    setup_fighters(1.0f, 11);
    fixture_motion_after_query = 12;
    assert(profile_ftLib_800864A8(&fixture_origin, fixture_owner) == 1.0f);
    assert(fixture_rng_calls == 0);
    assert_queries_in_source_order();
    setup_fighters(-1.0f, 12);
    fixture_fighters[3].player_id = fixture_fighters[0].player_id;
    assert(profile_ftLib_800864A8(&fixture_origin, fixture_owner) == 1.0f);
    assert(fixture_query_calls == 2 && fixture_query_order[0] == 1 && fixture_query_order[1] == 2);
    assert(fixture_rng_calls == 1);
    puts("WHISPY_SIGNED_THRESHOLD_QUERY_ORDER_AND_SAME_PLAYER_FILTER");

    setup_fighters(1.0f, 12);
    float rebirth = profile_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert(rebirth == 1.0f);
    assert(fixture_rng_calls == 0);
    assert_queries_in_source_order();

    setup_fighters(0.0f, 12);
    float equal_x = profile_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert(equal_x == 1.0f);
    assert(fixture_rng_calls == 0);
    assert_queries_in_source_order();

    setup_fighters(-1.0f, 11);
    fixture_team_mode = 1;
    fixture_fighters[0].team = 3;
    fixture_fighters[1].team = 4;
    fixture_fighters[2].team = 3;
    fixture_fighters[3].team = 5;
    fixture_fighters[2].synthetic_bone_x = -50.0f;
    fixture_fighters[3].x221F_b3 = 1;
    fixture_fighters[3].synthetic_bone_x = -60.0f;
    float filtered = profile_ftLib_800864A8(&fixture_origin, fixture_owner);
    assert(filtered == 1.0f);
    assert(fixture_rng_calls == 0);
    assert(fixture_query_calls == 1 && fixture_query_order_count == 1);
    assert(fixture_query_order[0] == 1);
    puts("WHISPY_FILTERS_AND_NONZERO_NO_RNG_CONTROL");
    puts("Whispy actual-source reduction passed");
    return 0;
}
'''
    return preamble + "\n".join(funcs) + main


def _materialize_stadium(output):
    target = "src/melee/gr/grpstadium.c"
    original_bytes = (MELEE / target).read_bytes()
    if _sha(original_bytes) != "3a69ef5e8bbaa7b5357cd56262cbf3b8d88b6ae432db0a41ce6de1ef0b548c24":
        raise AssertionError("pinned grpstadium.c changed")
    commit = subprocess.check_output(["git", "-C", str(MELEE), "rev-parse", "HEAD"], text=True, timeout=30).strip()
    if commit != EXPECTED_MELEE_COMMIT:
        raise AssertionError("pinned Melee commit changed")
    tree = output / "source-reduction"
    path = tree / target
    path.parent.mkdir(parents=True)
    path.write_bytes(original_bytes)
    commands = [["git", "init", "-q"],
                ["git", "apply", "--check", f"--include={target}", str(BASE_PATCH)],
                ["git", "apply", f"--include={target}", str(BASE_PATCH)]]
    for command in commands:
        subprocess.run(command, cwd=tree, check=True, timeout=30)
    import sys
    sys.path.insert(0, str(ROOT / "scripts"))
    import gameplay_bool
    path.write_text(gameplay_bool.transform_source(Path(target), path.read_text()))
    base_bytes = path.read_bytes()
    for command in [["git", "apply", "--check", f"--include={target}", str(PROFILE_PATCH)],
                    ["git", "apply", f"--include={target}", str(PROFILE_PATCH)]]:
        subprocess.run(command, cwd=tree, check=True, timeout=30)
    original = original_bytes.decode()
    profile = path.read_text()
    def case8(source):
        function = _function(source, "void grStadium_801D2344(Ground_GObj* g)")
        start = function.index("    case 8:\n")
        return function[start:function.rindex("\n    }")]
    original_case = case8(original)
    profile_case = case8(profile)
    # Remove exactly the added conditional; its nonprofile arm must be the
    # untouched original source gate. This is an omission control, not a clone
    # of the new helper's implementation.
    start = profile_case.index("#ifdef MELEE_WEB_SLIPPI_PROFILE\n")
    split = profile_case.index("#else\n", start)
    end = profile_case.index("#endif\n", split)
    omitted_case = profile_case[:start] + profile_case[split + len("#else\n"):end] + profile_case[end + len("#endif\n"):]
    if omitted_case != original_case:
        raise AssertionError("Stadium nonprofile source gate changed")
    helper = _function(original, "bool grStadium_801D32D0(Ground_GObj* gobj)")
    helper = gameplay_bool.transform_source(Path(target), helper)
    if helper.count("PAD_STACK(4);") != 1:
        raise AssertionError("original projection helper stack annotation changed")
    helper = helper.replace("PAD_STACK(4);", "(void) 0; /* original PAD_STACK(4), storage only */", 1)
    transition = _function(original, "void grStadium_801D2A60(Ground_GObj* gobj)")
    pointer_capture = profile_case.index("Fighter* slippi_player = slippi_player_gobj->user_data;")
    helper_call = profile_case.index("(void) grStadium_801D32D0(gobj);")
    position_read = profile_case.index("slippi_player->cur_pos.x")
    if not pointer_capture < helper_call < position_read:
        raise AssertionError("Stadium pointer capture/helper/position order changed")
    if transition.count("HSD_Randf()") != 1 or "do {" not in transition or "} while (" not in transition:
        raise AssertionError("original Stadium transition RNG loop changed")
    identity = {"melee_commit": commit, "original_file_sha256": _sha(original_bytes),
                "base_bool_file_sha256": _sha(base_bytes), "profile_file_sha256": _sha(path.read_bytes()),
                "original_case_sha256": _sha(original_case.encode()),
                "profile_case_sha256": _sha(profile_case.encode()),
                "omitted_case_sha256": _sha(omitted_case.encode()),
                "projection_helper_sha256": _sha(helper.encode()),
                "transition_function_sha256": _sha(transition.encode()),
                "overlay_sha256": _sha(PROFILE_PATCH.read_bytes())}
    (output / "source_identity.json").write_text(json.dumps(identity, indent=2) + "\n")
    return original_case, profile_case, omitted_case, helper, transition


STADIUM_PREAMBLE = r'''/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual case-8 gate, camera helper and transition/Randf loop from pinned
 * doldecomp/melee b43912cc. Host storage and external services are synthetic.
 * Vec3 uses the repository gameplay compatibility boundary. No SDK layout claim.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "slippi_profile/ps_camera_independent_monitor_fix.h"
typedef int melee_source_bool;
typedef struct Fighter { Vec3 cur_pos; } Fighter;
typedef struct HSD_GObj { void *user_data; void *hsd_obj; } HSD_GObj;
typedef HSD_GObj Ground_GObj;
typedef struct ImageDescWrapper { u16 flag : 1; u16 x1A, x1C; } ImageDescWrapper;
typedef struct HSD_CObj { struct { float xmin, xmax, ymin, ymax; } viewport; } HSD_CObj;
/* These accessed field types follow grStadium_Display, not absolute host offsets. */
typedef struct Ground { struct { struct {
    HSD_GObj *xDC; int xE0; s16 xE4, xEA, xEE, xF2;
} display; } u; } Ground;
#define GET_GROUND(gobj) ((Ground*) (gobj)->user_data)
#define GET_WRAPPER(gobj) ((ImageDescWrapper*) (gobj)->user_data)
#define GET_COBJ(gobj) ((HSD_CObj*) (gobj)->hsd_obj)
static struct { s16 x48, x4A, x4C, x4E, x50; } fixture_parameters;
#define yakumono_param (&fixture_parameters)
static Ground ground;
static HSD_GObj stage_gobj, wrapper_gobj, camera_gobj, player_gobj, replacement_gobj;
static Fighter player, replacement;
static ImageDescWrapper wrapper;
static HSD_CObj camera;
static unsigned player_queries, dead_queries, camera_queries, bone_queries, projections;
static unsigned rng_calls, setter_calls;
static int requested_state, selected_missing, selected_dead, projection_lane;
static int order_probe, use_replacement;
static float rng_trace[4];
static HSD_GObj* Player_GetEntity(s32 slot) {
    assert(slot == 0); player_queries++;
    return selected_missing ? NULL : (use_replacement ? &replacement_gobj : &player_gobj);
}
static int Player_8003219C(s32 slot) { assert(slot == 0); dead_queries++; return selected_dead; }
static HSD_GObj* Camera_80030A50(void) {
    camera_queries++; assert(wrapper.flag == 0); return &camera_gobj;
}
static void ftLib_80086B90(HSD_GObj *gobj, Vec3 *position) {
    assert(gobj == &player_gobj); bone_queries++;
    *position = (Vec3){1.0f, 2.0f, 3.0f};
}
static void lbVector_WorldToScreen(HSD_CObj *cobj, Vec3 *world, Vec3 *screen, int mode) {
    assert(cobj == &camera && mode == 0);
    assert(world->x == 1.0f && world->y == 2.0f && world->z == 3.0f);
    projections++; screen->x = projection_lane ? 20.0f : 320.0f;
    screen->y = 240.0f; screen->z = 0.0f;
    if (order_probe) {
        /* Synthetic ordering probe: early coordinate copy and late entity
         * lookup would both choose the wrong result. Not a gameplay witness. */
        player.cur_pos.x = 121.0f; replacement.cur_pos.x = 0.0f; use_replacement = 1;
    }
}
static float HSD_Randf(void) {
    const float sequence[3] = {0.0f, 0.25f, 0.5f};
    assert(rng_calls < 3); float value = sequence[rng_calls];
    rng_trace[rng_calls++] = value; return value;
}
static void grStadium_801D2528(Ground_GObj *gobj, int state, int arg) {
    assert(gobj == &stage_gobj && arg == 0 && state == 1);
    setter_calls++; requested_state = state;
    /* Boundary observer only: the large downstream mode setter is unrun. */
}
'''

STADIUM_MAIN = r'''
static void reset_lane(int lane, float x, float y) {
    memset(&ground, 0, sizeof ground); memset(&player, 0, sizeof player);
    memset(&replacement, 0, sizeof replacement);
    stage_gobj.user_data = &ground; wrapper_gobj.user_data = &wrapper;
    player_gobj.user_data = &player; replacement_gobj.user_data = &replacement;
    camera.viewport.xmin = 0.0f; camera.viewport.xmax = 640.0f;
    camera.viewport.ymin = 0.0f; camera.viewport.ymax = 480.0f;
    camera_gobj.hsd_obj = &camera;
    ground.u.display.xDC = &wrapper_gobj; ground.u.display.xE0 = 5;
    ground.u.display.xE4 = 8; ground.u.display.xEA = 7;
    fixture_parameters.x48 = 1; fixture_parameters.x4A = 1;
    fixture_parameters.x4C = 1; fixture_parameters.x4E = 1; fixture_parameters.x50 = 10;
    wrapper.flag = 1; wrapper.x1A = wrapper.x1C = 65535;
    player.cur_pos = (Vec3){x, y, 0.0f};
    player_queries = dead_queries = camera_queries = bone_queries = projections = 0;
    rng_calls = setter_calls = 0; requested_state = -1;
    selected_missing = selected_dead = 0;
    projection_lane = lane; order_probe = use_replacement = 0;
    memset(rng_trace, 0, sizeof rng_trace);
}
static void assert_original_transition_rng(void) {
    assert(rng_calls == 3 && setter_calls == 1 && requested_state == 1);
    assert(rng_trace[0] == 0.0f && rng_trace[1] == 0.25f && rng_trace[2] == 0.5f);
    assert(ground.u.display.xF2 == 1);
}
static void assert_helper(int profile) {
    assert(ground.u.display.xE0 == 4 && wrapper.flag == 0);
    assert(player_queries == (unsigned)(profile ? 4 : 3));
    assert(dead_queries == 3 && camera_queries == 1 && bone_queries == 1 && projections == 1);
    assert(wrapper.x1A == (projection_lane ? 0 : 258) && wrapper.x1C == 200);
}
int main(void) {
    reset_lane(0, 0.0f, 0.0f); original_case8(&stage_gobj);
    assert_helper(0); assert(setter_calls == 0 && rng_calls == 0 && ground.u.display.xF2 == 0);
    reset_lane(1, 0.0f, 0.0f); original_case8(&stage_gobj);
    assert_helper(0); assert_original_transition_rng();
    puts("STADIUM_ORIGINAL_CAMERA_DEPENDENT_BRANCH_RNG_RED");
    for (int lane = 0; lane < 2; ++lane) {
        reset_lane(lane, 0.0f, 0.0f); profile_case8(&stage_gobj); assert_helper(1);
        assert(setter_calls == 0 && rng_calls == 0 && ground.u.display.xF2 == 0);
        reset_lane(lane, 121.0f, 0.0f); profile_case8(&stage_gobj); assert_helper(1);
        assert_original_transition_rng();
    }
    puts("STADIUM_PROFILE_FIXED_DECISION_GREEN_PRESENTATION_REMAINS_VIEW_SPECIFIC");
    reset_lane(0, 0.0f, 0.0f); omitted_case8(&stage_gobj);
    assert(setter_calls == 0 && rng_calls == 0);
    reset_lane(1, 0.0f, 0.0f); omitted_case8(&stage_gobj); assert_original_transition_rng();
    puts("STADIUM_OMITTED_OVERRIDE_RESTORES_BRANCH_RNG_RED");
    const float edges[][3] = {
        {-120.0f, 0.0f, 1.0f}, {120.0f, 0.0f, 1.0f},
        {0.0f, -20.0f, 1.0f}, {0.0f, 80.0f, 1.0f},
        {-120.00000762939453125f, 0.0f, 0.0f}, {120.00000762939453125f, 0.0f, 0.0f},
        {0.0f, -20.0000019073486328125f, 0.0f}, {0.0f, 80.00000762939453125f, 0.0f},
        {-0.0f, 0.0f, 1.0f}
    };
    for (unsigned i = 0; i < sizeof edges/sizeof edges[0]; ++i) {
        reset_lane(1, edges[i][0], edges[i][1]); profile_case8(&stage_gobj); assert_helper(1);
        if (edges[i][2] == 0.0f) assert_original_transition_rng();
        else assert(setter_calls == 0 && rng_calls == 0);
    }
    union { uint32_t bits; float value; } nan = {0x7fc00000u};
    reset_lane(1, nan.value, 0.0f); profile_case8(&stage_gobj); assert(setter_calls == 0);
    reset_lane(1, 0.0f, nan.value); profile_case8(&stage_gobj); assert(setter_calls == 0);
    reset_lane(1, nan.value, 81.0f); profile_case8(&stage_gobj); assert_original_transition_rng();
    reset_lane(0, 0.0f, 0.0f); order_probe = 1; profile_case8(&stage_gobj);
    assert_helper(1); assert_original_transition_rng();
    puts("STADIUM_POINTER_BEFORE_HELPER_POSITION_AFTER_AND_FLOAT_EDGES");
    reset_lane(0, 0.0f, 0.0f); ground.u.display.xE0 = -1; profile_case8(&stage_gobj);
    assert(ground.u.display.xE0 == -2 && player_queries == 0 && camera_queries == 0 && wrapper.flag == 1);
    assert_original_transition_rng();
    reset_lane(0, 0.0f, 0.0f); selected_missing = 1; profile_case8(&stage_gobj);
    assert(player_queries == 1 && dead_queries == 0 && camera_queries == 0 && wrapper.flag == 1);
    assert_original_transition_rng();
    reset_lane(0, 0.0f, 0.0f); selected_dead = 1; profile_case8(&stage_gobj);
    assert(player_queries == 1 && dead_queries == 1 && camera_queries == 0 && wrapper.flag == 1);
    assert_original_transition_rng();
    /* The authored helper can return false with no CObj; its return is still
     * overridden. Outer wrapper/player ownership remains valid. */
    reset_lane(0, 0.0f, 0.0f); camera_gobj.hsd_obj = NULL; profile_case8(&stage_gobj);
    assert(camera_queries == 1 && bone_queries == 0 && projections == 0 && setter_calls == 0);
    assert(wrapper.flag == 0 && wrapper.x1A == 65535 && wrapper.x1C == 65535);
    puts("STADIUM_SOURCE_EARLY_GATES_AND_ORIGINAL_HELPER_SIDE_EFFECT_CONTROLS");
    puts("Stadium actual-source reduction passed; downstream mode setter and real projector/RNG service are synthetic");
    return 0;
}
'''


def _stadium_fixture(original, profile, omitted, helper, transition):
    def wrap(name, fragment):
        return ("static void " + name + "(Ground_GObj* g) {\n"
                "Ground_GObj* gobj = g; Ground* gp = GET_GROUND(gobj);\n"
                "ImageDescWrapper* temp_r3_8; switch (8) {\n" + fragment + "\n}\n}\n")
    return STADIUM_PREAMBLE + transition + helper + wrap("original_case8", original) + wrap("profile_case8", profile) + wrap("omitted_case8", omitted) + STADIUM_MAIN


class StageRenderReductionTests(unittest.TestCase):
    def _run_whispy_fixture(self, output):
        sign, original, profile, omitted = _materialize_whispy(output)
        source = output / "whispy_actual_source_reduction.c"
        executable = output / "whispy_actual_source_reduction"
        fixture_text = _fixture(sign, original, profile, omitted)
        source.write_text(fixture_text, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        command = [
            *compiler, "-std=c11", "-DTARGET_PC", "-DMELEE_WEB_SLIPPI_PROFILE=1",
            "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-fno-fast-math",
            "-include", str(ROOT / "src/gameplay_compat.h"),
            "-I", str(ROOT / "src"), "-I", str(MELEE / "src"),
            "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(MELEE / "extern/dolphin/include"),
            str(source),
            str(ROOT / "src/slippi_profile/whispy_direction_fix.c"),
            "-lm", "-o", str(executable),
        ]
        wrapped = ["python3", str(ROOT / "scripts/agent_workspace.py"), "run", "--", *command]
        compile_result = _run(wrapped, ROOT, capture_output=True, text=True)
        (output / "compile.stdout").write_text(compile_result.stdout, encoding="utf-8")
        (output / "compile.stderr").write_text(compile_result.stderr, encoding="utf-8")
        if compile_result.returncode:
            raise AssertionError(
                f"Whispy actual-source compile failed ({compile_result.returncode}); "
                f"retained {output}:\n{compile_result.stdout}\n{compile_result.stderr}"
            )
        run_result = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True, timeout=15)
        (output / "run.stdout").write_text(run_result.stdout, encoding="utf-8")
        (output / "run.stderr").write_text(run_result.stderr, encoding="utf-8")
        if run_result.returncode:
            raise AssertionError(
                f"Whispy actual-source assertions failed ({run_result.returncode}); "
                f"retained {output}:\n{run_result.stdout}\n{run_result.stderr}"
            )
        required = (
            "WHISPY_ORIGINAL_SOURCE_CAUSAL_RED",
            "WHISPY_PROFILE_GREEN_RNG_ONCE_PER_TIE",
            "WHISPY_OMITTED_HOOK_CAUSAL_RED",
            "WHISPY_FILTERS_AND_NONZERO_NO_RNG_CONTROL",
        )
        for marker in required:
            if marker not in run_result.stdout:
                raise AssertionError(f"missing Whispy fixture marker {marker!r}; retained {output}")
        return {"output": str(output), "fixture_sha256": _sha(fixture_text.encode())}

    def test_whispy_actual_source_causal_red_profile_green_and_omission(self):
        work_root = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work")).resolve()
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="slippi-whispy-reduction-", dir=work_root))
        try:
            result = self._run_whispy_fixture(output)
            (output / "result.json").write_text(
                __import__("json").dumps(result, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        except Exception:
            raise
        else:
            if os.environ.get("MELEE_WEB_RETAIN_OUTPUT") != "1":
                shutil.rmtree(output)

    def test_ps_camera_independent_monitor_actual_source(self):
        work_root = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work")).resolve()
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="slippi-stadium-monitor-reduction-", dir=work_root))
        parts = _materialize_stadium(output)
        fixture = output / "stadium_actual_source_reduction.c"
        fixture.write_text(_stadium_fixture(*parts))
        executable = output / "stadium_actual_source_reduction"
        command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-DTARGET_PC",
            "-DMELEE_WEB_SLIPPI_PROFILE=1", "-Wall", "-Wextra", "-Werror",
            "-ffp-contract=off", "-fno-fast-math", "-include", str(ROOT / "src/gameplay_compat.h"),
            "-I", str(ROOT / "src"), "-I", str(MELEE / "src"),
            "-I", str(ROOT / ".deps/aurora/include"), "-I", str(MELEE / "extern/dolphin/include"),
            str(fixture), str(ROOT / "src/slippi_profile/ps_camera_independent_monitor_fix.c"),
            "-lm", "-o", str(executable)]
        (output / "command.json").write_text(json.dumps(command, indent=2) + "\n")
        compiled = _run(command, ROOT, capture_output=True, text=True)
        (output / "compile.stdout").write_text(compiled.stdout)
        (output / "compile.stderr").write_text(compiled.stderr)
        self.assertEqual(compiled.returncode, 0, f"retained {output}: {compiled.stdout}\n{compiled.stderr}")
        ran = subprocess.run([str(executable)], cwd=ROOT, capture_output=True, text=True, timeout=15)
        (output / "run.stdout").write_text(ran.stdout)
        (output / "run.stderr").write_text(ran.stderr)
        self.assertEqual(ran.returncode, 0, f"retained {output}: {ran.stdout}\n{ran.stderr}")
        for marker in ["STADIUM_ORIGINAL_CAMERA_DEPENDENT_BRANCH_RNG_RED",
            "STADIUM_PROFILE_FIXED_DECISION_GREEN_PRESENTATION_REMAINS_VIEW_SPECIFIC",
            "STADIUM_OMITTED_OVERRIDE_RESTORES_BRANCH_RNG_RED",
            "STADIUM_POINTER_BEFORE_HELPER_POSITION_AFTER_AND_FLOAT_EDGES",
            "STADIUM_SOURCE_EARLY_GATES_AND_ORIGINAL_HELPER_SIDE_EFFECT_CONTROLS"]:
            self.assertIn(marker, ran.stdout)
        (output / "result.json").write_text(json.dumps({"result":"PASS_ACTUAL_SOURCE_REDUCTION",
            "fixture_sha256":_sha(fixture.read_bytes()),"scope":"synthetic services; no stage runtime"}, indent=2) + "\n")
        if os.environ.get("MELEE_WEB_RETAIN_OUTPUT") != "1":
            shutil.rmtree(output)
