# SPDX-License-Identifier: GPL-3.0-or-later
"""Native reduction of the exact profile-gated Fighter_8006A360 timer block."""

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(os.environ.get(
    "MELEE_WEB_TEST_ROOT", Path(__file__).resolve().parents[3],
)).resolve()
PIVOT = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work")).resolve()

BASE_PATCH = ROOT / "patches/melee-gameplay.patch"
PINNED_FIGHTER = ROOT / ".deps/melee/src/melee/ft/fighter.c"
PROFILE_HELPER = ROOT / "src/slippi_profile/offscreen_damage.c"
PROFILE_OVERLAY = ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"
GM_FORWARD = ROOT / ".deps/melee/src/melee/gm/forward.h"
FT_COMMON_FORWARD = ROOT / ".deps/melee/src/melee/ft/kinds/ftCommon/forward.h"
FIGHTER_TYPES = ROOT / ".deps/melee/src/melee/ft/types.h"
DEPENDENCY_LOCK = ROOT / "dependencies.lock.json"

EXPECTED = {
    "melee_source_commit": "b43912cc78606f96c9569f5d6229bc9d7e265ea5",
    "timer_block_sha256": "e41438c2abea413c96f035e63c975c0ded58eb89a62d8da0b3a4b13c92edcca5",
}


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _file_sha256(path):
    return _sha256(path.read_bytes())


def _matching_brace(source, opening):
    """Return the exclusive end of a C brace block, ignoring comments/strings."""
    if source[opening] != "{":
        raise AssertionError("brace parser did not start at an opening brace")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                state = "code"
                index += 1
        elif state == "string":
            if char == "\\":
                index += 1
            elif char == '"':
                state = "code"
        elif state == "character":
            if char == "\\":
                index += 1
            elif char == "'":
                state = "code"
        elif char == "/" and next_char == "/":
            state = "line_comment"
            index += 1
        elif char == "/" and next_char == "*":
            state = "block_comment"
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
    raise AssertionError("unterminated C brace block")




def _git(repo, *arguments):
    command = ["git", "-C", str(repo), *arguments]
    result = subprocess.run(
        command, cwd=ROOT, check=False, capture_output=True, text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command {command!r} exited {result.returncode}: "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    return result


def _materialize_profile_source(output):
    """Apply the reviewed source patches to a disposable copy of the pinned file."""
    source_repo = output / "source-reduction"
    fighter = source_repo / "src/melee/ft/fighter.c"
    fighter.parent.mkdir(parents=True)
    shutil.copyfile(PINNED_FIGHTER, fighter)

    status = subprocess.check_output(
        ["git", "-C", str(ROOT / ".deps/melee"), "status", "--porcelain",
         "--", "src/melee/ft/fighter.c", "src/melee/gm/forward.h",
         "src/melee/ft/types.h", "src/melee/ft/kinds/ftCommon/forward.h"],
        cwd=ROOT, text=True,
    )
    if status:
        raise AssertionError(
            f"pinned source files have local changes; refusing extraction: {status!r}"
        )

    _git(source_repo, "init", "-q")

    target = "src/melee/ft/fighter.c"
    _git(source_repo, "apply", "--check", f"--include={target}", str(BASE_PATCH))
    _git(source_repo, "apply", f"--include={target}", str(BASE_PATCH))
    _git(source_repo, "apply", "--check", f"--include={target}", str(PROFILE_OVERLAY))
    _git(source_repo, "apply", f"--include={target}", str(PROFILE_OVERLAY))

    composed = fighter.read_text(encoding="utf-8")
    required_once = (
        "#include <slippi_profile/offscreen_damage.h>",
        "slippi_profile_offscreen_damage_zone(&zone)",
        "ifMagnify_802FC998(fp->player_id)",
    )
    for expected in required_once:
        if composed.count(expected) != 1:
            raise AssertionError(
                f"composed source include/context mismatch for {expected!r}: "
                f"count={composed.count(expected)}"
            )
    return fighter


def _extract_source(profile_fighter_path):
    source_bytes = profile_fighter_path.read_bytes()
    source = source_bytes.decode("utf-8")
    helper_bytes = PROFILE_HELPER.read_bytes()
    types_text = FIGHTER_TYPES.read_text(encoding="utf-8")
    overlay_bytes = PROFILE_OVERLAY.read_bytes()
    forward_bytes = GM_FORWARD.read_bytes()

    observed = {
        "profile_fighter_sha256": _sha256(source_bytes),
        "base_patch_sha256": _file_sha256(BASE_PATCH),
        "fighter_types_sha256": _file_sha256(FIGHTER_TYPES),
        "pinned_fighter_sha256": _file_sha256(PINNED_FIGHTER),
        "profile_helper_sha256": _sha256(helper_bytes),
        "profile_overlay_sha256": _sha256(overlay_bytes),
        "gm_forward_sha256": _sha256(forward_bytes),
        "ft_common_forward_sha256": _file_sha256(FT_COMMON_FORWARD),
    }
    pinned_lock = json.loads(DEPENDENCY_LOCK.read_text(encoding="utf-8"))
    lock_commit = pinned_lock["repositories"]["melee"]["commit"]
    actual_commit = subprocess.check_output(
        ["git", "-C", str(ROOT / ".deps/melee"), "rev-parse", "HEAD"],
        cwd=ROOT, text=True,
    ).strip()
    if lock_commit != EXPECTED["melee_source_commit"] or actual_commit != lock_commit:
        raise AssertionError(
            f"pinned Melee source commit mismatch: lock={lock_commit}, actual={actual_commit}"
        )

    source_field_declarations = (
        "/* +7AC */ int x7AC;",
        "/* +7B0 */ int x7B0;",
        "/* +7B4 */ int x7B4_unkDamage;",
        "/* fp+1830 */ float x1830_percent;",
        "/* fp+1910 */ int x1910;",
    )
    for declaration in source_field_declarations:
        if types_text.count(declaration) != 1:
            raise AssertionError(
                f"actual fighter field layout changed for {declaration!r}: "
                f"count={types_text.count(declaration)}"
            )

    function_signature = "void Fighter_8006A360(Fighter_GObj* gobj)"
    function_start = source.index(function_signature)
    function_open = source.index("{", function_start)
    function_end = _matching_brace(source, function_open)
    function = source[function_start:function_end]

    timer_marker = "if (!fp->x221F_b4 && Camera_80031144() == 1.0f) {"
    if function.count(timer_marker) != 1:
        raise AssertionError("expected one exact offscreen timer gate in Fighter_8006A360")
    timer_start = function_start + function.index(timer_marker)
    timer_open = source.index("{", timer_start)
    timer_end = _matching_brace(source, timer_open)
    timer_block = source[timer_start:timer_end]

    outer_marker = "if (!fp->x221F_b3) {"
    outer_candidates = []
    search_at = function_start
    while True:
        position = source.find(outer_marker, search_at, timer_start)
        if position < 0:
            break
        opening = source.index("{", position, position + len(outer_marker))
        end = _matching_brace(source, opening)
        if position < timer_start < timer_end < end:
            outer_candidates.append(position)
        search_at = position + 1
    if len(outer_candidates) != 1:
        raise AssertionError(
            f"expected one source outer gate to enclose timer block, got {outer_candidates}"
        )
    outer_start = outer_candidates[0]
    outer_end = source.index("{", outer_start, outer_start + len(outer_marker)) + 1
    outer_gate = source[outer_start:outer_end]
    if outer_gate != outer_marker:
        raise AssertionError(f"outer gate source changed: {outer_gate!r}")

    block_hash = _sha256(timer_block.encode("utf-8"))
    if block_hash != EXPECTED["timer_block_sha256"]:
        raise AssertionError(
            f"extracted source block changed: expected "
            f"{EXPECTED['timer_block_sha256']}, observed {block_hash}"
        )
    for required in (
        "#ifdef MELEE_WEB_SLIPPI_PROFILE",
        "slippi_profile_offscreen_damage_zone(&zone)",
        "#else",
        "ifMagnify_802FC998(fp->player_id)",
        "fp->dmg.x1910++",
        "fp->dmg.x1910 = 0;",
        "p_ftCommonData->x7AC",
        "Fighter_TakeDamage_8006CC7C",
        "p_ftCommonData->x7B4_unkDamage",
    ):
        if required not in timer_block:
            raise AssertionError(f"real source block lost required text {required!r}")

    mode_match = re.search(
        r"/\*\s*([0-9A-Fa-f]+)\s*\*/\s*GM_HOME_RUN_CONTEST\s*,",
        forward_bytes.decode("utf-8"),
    )
    if not mode_match:
        raise AssertionError("could not derive HRC enum from pinned source header")
    hrc_game_mode = int(mode_match.group(1), 16)

    # Inputs come from the pinned game's authored states, independently of the
    # helper under test. A wrong helper exclusion must fail the native control.
    enum_text = FT_COMMON_FORWARD.read_text(encoding="utf-8")
    enum_start = enum_text.index("typedef enum ftCommon_MotionState {")
    enum_start = enum_text.index("{", enum_start) + 1
    enum_end = enum_text.index("    ftCo_MS_DeadUpFallHitCamera,", enum_start)
    authored_states = {}
    value = -1
    for row in enum_text[enum_start:enum_end].splitlines():
        if not row.strip():
            continue
        match = re.fullmatch(r"\s*(ftCo_MS_\w+)(?:\s*=\s*(-?\d+))?,\s*", row)
        if match is None:
            raise AssertionError(f"unsupported authored motion-state declaration: {row!r}")
        value = int(match.group(2)) if match.group(2) is not None else value + 1
        authored_states[match.group(1)] = value
    action_values = {
        "ACTION_STATE_STAR_KO": authored_states["ftCo_MS_DeadUpStar"],
        "ACTION_STATE_SCREEN_KO": authored_states["ftCo_MS_DeadUpFall"],
    }

    return {
        "source_bytes": source_bytes,
        "profile_fighter_path": str(profile_fighter_path),
        "helper_bytes": helper_bytes,
        "timer_block": timer_block,
        "timer_block_sha256": block_hash,
        "outer_gate": outer_gate,
        "source_line_start": source[:timer_start].count("\n") + 1,
        "source_line_end": source[:timer_end].count("\n") + 1,
        "outer_gate_line": source[:outer_start].count("\n") + 1,
        "hrc_game_mode": hrc_game_mode,
        "action_values": action_values,
        "observed_source_hashes": observed,
        "source_field_declarations": list(source_field_declarations),
    }


def _fixture_source(extracted):
    timer_block = extracted["timer_block"]
    outer_gate = extracted["outer_gate"]
    hrc_game_mode = extracted["hrc_game_mode"]
    star_ko = extracted["action_values"]["ACTION_STATE_STAR_KO"]
    screen_ko = extracted["action_values"]["ACTION_STATE_SCREEN_KO"]
    fixture = r'''/* Native source reduction. All fixture services and values are synthetic.
 * Only named services below are defined; new source calls fail at compile/link. */
#include <assert.h>
#include <stdio.h>
#include "slippi_profile/offscreen_damage.h"

#define GM_HOME_RUN_CONTEST @HRC_MODE@
#define ACTION_STATE_STAR_KO @STAR_KO@
#define ACTION_STATE_SCREEN_KO @SCREEN_KO@
#define FIXTURE_THRESHOLD_T 3
#define FIXTURE_DAMAGE_VALUE 13
#define FIXTURE_CAMERA_LEFT (-10.0f)
#define FIXTURE_CAMERA_RIGHT 10.0f
#define FIXTURE_CAMERA_TOP 10.0f
#define FIXTURE_CAMERA_BOTTOM (-10.0f)
#define FIXTURE_PERCENT_LIMIT 20

typedef struct FixturePosition { float x, y; } FixturePosition;
typedef struct FixtureDamage {
    float x1830_percent;
    int x1910;
} FixtureDamage;
typedef struct FixtureFighter {
    int x221F_b3;
    int x221F_b4;
    int x221F_b1;
    int motion_id;
    int player_id;
    FixturePosition cur_pos;
    FixtureDamage dmg;
} Fighter;
typedef struct FixtureCommonData {
    int x7B0;
    int x7AC;
    int x7B4_unkDamage;
} FixtureCommonData;

static Fighter fixture_fighter;
static FixtureCommonData fixture_common;
static int fixture_game_mode;
static int fixture_scene;
static int fixture_player_flag;
static int fixture_legacy_cache;
static float fixture_camera_factor;
static float fixture_left;
static float fixture_right;
static float fixture_top;
static float fixture_bottom;
static unsigned camera_calls;
static unsigned mode_calls;
static unsigned scene_calls;
static unsigned player_flag_calls;
static unsigned legacy_cache_calls;
static unsigned bounds_calls;
static unsigned damage_calls;
static Fighter *last_damage_fighter;
static float last_damage_value;

static int gm_GetCurrentGameMode(void) { mode_calls++; return fixture_game_mode; }
static int gm_GetCurrentSceneIndex(void) { scene_calls++; return fixture_scene; }
static float Camera_80031144(void) { camera_calls++; return fixture_camera_factor; }
static float Stage_GetCamBoundsLeftOffset(void) { bounds_calls++; return fixture_left; }
static float Stage_GetCamBoundsRightOffset(void) { bounds_calls++; return fixture_right; }
static float Stage_GetCamBoundsTopOffset(void) { bounds_calls++; return fixture_top; }
static float Stage_GetCamBoundsBottomOffset(void) { bounds_calls++; return fixture_bottom; }
static int Player_GetMoreFlagsBit3(int player_id)
{
    assert(player_id == fixture_fighter.player_id);
    player_flag_calls++;
    return fixture_player_flag;
}
static int ifMagnify_802FC998(int player_id)
{
    assert(player_id == fixture_fighter.player_id);
    legacy_cache_calls++;
    return fixture_legacy_cache;
}
static void Fighter_TakeDamage_8006CC7C(Fighter *fighter, float damage)
{
    damage_calls++;
    last_damage_fighter = fighter;
    last_damage_value = damage;
}

#define MELEE_WEB_SLIPPI_PROFILE 1
static void run_profile_block(void)
{
    Fighter *fp = &fixture_fighter;
    FixtureCommonData *p_ftCommonData = &fixture_common;
    @OUTER_GATE@
@TIMER_BLOCK@
    }
}
#undef MELEE_WEB_SLIPPI_PROFILE

static void run_legacy_block(void)
{
    Fighter *fp = &fixture_fighter;
    FixtureCommonData *p_ftCommonData = &fixture_common;
    @OUTER_GATE@
@TIMER_BLOCK@
    }
}

static void reset_fixture(void)
{
    fixture_fighter = (Fighter){0};
    fixture_common = (FixtureCommonData){0};
    fixture_game_mode = 0;
    fixture_scene = 0;
    fixture_player_flag = 1;
    fixture_legacy_cache = 0;
    fixture_camera_factor = 1.0f;
    fixture_left = FIXTURE_CAMERA_LEFT;
    fixture_right = FIXTURE_CAMERA_RIGHT;
    fixture_top = FIXTURE_CAMERA_TOP;
    fixture_bottom = FIXTURE_CAMERA_BOTTOM;
    fixture_common.x7B0 = FIXTURE_PERCENT_LIMIT;
    fixture_common.x7AC = FIXTURE_THRESHOLD_T;
    fixture_common.x7B4_unkDamage = FIXTURE_DAMAGE_VALUE;
    fixture_fighter.player_id = 2;
    fixture_fighter.cur_pos.x = fixture_right + 1.0f;
    fixture_fighter.cur_pos.y = 0.0f;
    camera_calls = mode_calls = scene_calls = 0;
    player_flag_calls = legacy_cache_calls = bounds_calls = 0;
    damage_calls = 0;
    last_damage_fighter = NULL;
    last_damage_value = 0.0f;
}

static void assert_no_damage(void)
{
    assert(damage_calls == 0);
    assert(last_damage_fighter == NULL);
}

static void test_threshold_damage_and_reset(void)
{
    reset_fixture();
    for (int tick = 1; tick < FIXTURE_THRESHOLD_T; tick++) {
        run_profile_block();
        assert(fixture_fighter.dmg.x1910 == tick);
        assert_no_damage();
    }
    run_profile_block();
    assert(damage_calls == 1);
    assert(last_damage_fighter == &fixture_fighter);
    assert(last_damage_value == FIXTURE_DAMAGE_VALUE);
    assert(fixture_fighter.dmg.x1910 == 0);
    assert(legacy_cache_calls == 0);
}

static void test_stale_legacy_cache_difference(void)
{
    reset_fixture();
    fixture_fighter.dmg.x1910 = 0;
    run_profile_block();
    assert(fixture_fighter.dmg.x1910 == 1);
    assert(legacy_cache_calls == 0);
    assert_no_damage();

    reset_fixture();
    fixture_fighter.dmg.x1910 = 0;
    fixture_legacy_cache = 0; /* Explicit stale render-cache result. */
    run_legacy_block();
    assert(fixture_fighter.dmg.x1910 == 0);
    assert(legacy_cache_calls == 1);
    assert(mode_calls == 0);
    assert(scene_calls == 0);
    assert(bounds_calls == 0);
    assert_no_damage();
}

static void assert_zone_refusal_resets(void)
{
    fixture_fighter.dmg.x1910 = 2;
    run_profile_block();
    assert(fixture_fighter.dmg.x1910 == 0);
    assert_no_damage();
}

static void test_strict_bounds_and_inside_reset(void)
{
    reset_fixture();
    fixture_fighter.cur_pos.x = fixture_left;
    assert_zone_refusal_resets();
    reset_fixture();
    fixture_fighter.cur_pos.x = fixture_right;
    assert_zone_refusal_resets();
    reset_fixture();
    fixture_fighter.cur_pos.x = 0.0f;
    fixture_fighter.cur_pos.y = fixture_top;
    assert_zone_refusal_resets();
    reset_fixture();
    fixture_fighter.cur_pos.x = 0.0f;
    fixture_fighter.cur_pos.y = fixture_bottom;
    assert_zone_refusal_resets();
    reset_fixture();
    fixture_fighter.cur_pos.x = 0.0f;
    fixture_fighter.cur_pos.y = 0.0f;
    assert_zone_refusal_resets();
}

static void test_helper_exclusions_and_player_flag_reset(void)
{
    reset_fixture();
    fixture_game_mode = GM_HOME_RUN_CONTEST;
    fixture_scene = 1;
    assert_zone_refusal_resets();

    reset_fixture();
    fixture_fighter.x221F_b1 = 1;
    assert_zone_refusal_resets();

    reset_fixture();
    fixture_fighter.motion_id = ACTION_STATE_STAR_KO;
    assert_zone_refusal_resets();

    reset_fixture();
    fixture_fighter.motion_id = ACTION_STATE_SCREEN_KO;
    assert_zone_refusal_resets();

    reset_fixture();
    fixture_player_flag = 0;
    assert_zone_refusal_resets();
}

static void test_actual_outer_gates_preserve_timer(void)
{
    reset_fixture();
    fixture_fighter.dmg.x1910 = 2;
    fixture_fighter.x221F_b3 = 1;
    run_profile_block();
    assert(fixture_fighter.dmg.x1910 == 2);
    assert(camera_calls == 0);
    assert(player_flag_calls == 0);
    assert_no_damage();

    reset_fixture();
    fixture_fighter.dmg.x1910 = 2;
    fixture_fighter.x221F_b4 = 1;
    run_profile_block();
    assert(fixture_fighter.dmg.x1910 == 2);
    assert(camera_calls == 0);
    assert(player_flag_calls == 0);
    assert_no_damage();

    reset_fixture();
    fixture_fighter.dmg.x1910 = 2;
    fixture_camera_factor = 0.5f;
    run_profile_block();
    assert(fixture_fighter.dmg.x1910 == 2);
    assert(camera_calls == 1);
    assert(player_flag_calls == 0);
    assert_no_damage();

    reset_fixture();
    fixture_fighter.dmg.x1910 = 2;
    fixture_fighter.dmg.x1830_percent = fixture_common.x7B0;
    run_profile_block();
    assert(fixture_fighter.dmg.x1910 == 2);
    assert(camera_calls == 1);
    assert(player_flag_calls == 0);
    assert_no_damage();
}

int main(void)
{
    test_threshold_damage_and_reset();
    test_stale_legacy_cache_difference();
    test_strict_bounds_and_inside_reset();
    test_helper_exclusions_and_player_flag_reset();
    test_actual_outer_gates_preserve_timer();
    puts("offscreen timer source reduction passed");
    return 0;
}
'''
    return (
        fixture.replace("@HRC_MODE@", str(hrc_game_mode))
        .replace("@STAR_KO@", str(star_ko))
        .replace("@SCREEN_KO@", str(screen_ko))
        .replace("@OUTER_GATE@", outer_gate)
        .replace("@TIMER_BLOCK@", timer_block)
    )


class OffscreenTimerBlockTests(unittest.TestCase):
    def test_pinned_real_block_compiles_with_actual_helper_and_exact_gates(self):
        output_root = PIVOT
        output_root.mkdir(parents=True, exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="offscreen-timer-reduction-", dir=output_root))
        try:
            profile_fighter_path = _materialize_profile_source(output)
            extracted = _extract_source(profile_fighter_path)
        except Exception as error:
            self.fail(
                f"could not compose the exact pinned source block: {error}; "
                f"source scratch retained at {output}"
            )
        fixture = _fixture_source(extracted)
        (output / "extracted_timer_block.inc").write_text(
            extracted["timer_block"], encoding="utf-8",
        )
        (output / "fixture.c").write_text(fixture, encoding="utf-8")
        shutil.copyfile(profile_fighter_path, output / "pinned_profile_fighter.c")
        shutil.copyfile(PROFILE_HELPER, output / "pinned_offscreen_damage.c")
        shutil.copyfile(PROFILE_OVERLAY, output / "pinned_profile_overlay.patch")
        shutil.copyfile(BASE_PATCH, output / "pinned_base_gameplay.patch")
        shutil.copyfile(GM_FORWARD, output / "pinned_gm_forward.h")
        shutil.copyfile(FT_COMMON_FORWARD, output / "pinned_ft_common_forward.h")
        shutil.copyfile(PINNED_FIGHTER, output / "pinned_pristine_fighter.c")
        metadata = {
            **extracted["observed_source_hashes"],
            "melee_source_commit": EXPECTED["melee_source_commit"],
            "profile_fighter_path": extracted["profile_fighter_path"],
            "source_preparation": (
                "copy pinned .deps/melee fighter.c to owned scratch; "
                "git apply --check/apply only src/melee/ft/fighter.c from the "
                "reviewed gameplay patch then profile overlay"
            ),
            "timer_block_sha256": extracted["timer_block_sha256"],
            "outer_gate": extracted["outer_gate"],
            "outer_gate_line": extracted["outer_gate_line"],
            "timer_block_lines": [
                extracted["source_line_start"], extracted["source_line_end"],
            ],
            "synthetic_fixture": {
                "threshold_T": 3,
                "camera_bounds": [-10.0, 10.0, 10.0, -10.0],
                "damage_argument": 13,
                "percent_limit": 20.0,
            },
            "source_field_declarations": extracted["source_field_declarations"],
            "derived_source_constants": {
                "GM_HOME_RUN_CONTEST": extracted["hrc_game_mode"],
                **extracted["action_values"],
            },
        }
        (output / "extraction.json").write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8",
        )

        executable = output / "offscreen-timer-fixture"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        command = [
            *compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "src"),
            str(PROFILE_HELPER), str(output / "fixture.c"),
            "-o", str(executable),
        ]
        wrapped = [
            sys.executable, str(ROOT / "scripts/agent_workspace.py"),
            "run", "--", *command,
        ]
        try:
            subprocess.run(wrapped, cwd=ROOT, check=True)
            result = subprocess.run(
                [str(executable)], cwd=ROOT, check=False,
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                raise RuntimeError(
                    f"native fixture exited {result.returncode}: "
                    f"stdout={result.stdout!r}, stderr={result.stderr!r}"
                )
        except Exception as error:
            self.fail(
                f"real source timer reduction failed: {error}; "
                f"source and compiler reproducer retained at {output}"
            )
        self.assertEqual(result.stdout.strip(), "offscreen timer source reduction passed")
        if os.environ.get("MELEE_WEB_RETAIN_OUTPUT") != "1":
            shutil.rmtree(output)


if __name__ == "__main__":
    unittest.main()
