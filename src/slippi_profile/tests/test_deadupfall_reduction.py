# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual-source DeadUpFall v2 phase, physics, and render reductions."""

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
PIN = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"
MELEE = ROOT / ".deps/melee"
BASE_PATCH = ROOT / "patches/melee-gameplay.patch"
PROFILE_PATCH = ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"
FIXTURE = ROOT / "src/slippi_profile/tests/deadupfall_reduction_fixture.c.in"
SOURCE_PATHS = {
    "anim_physics": "src/melee/ft/ft_0D31.c",
    "draw": "src/melee/ft/ftdrawcommon.c",
    "fall": "src/melee/ft/ftcommon.c",
    "vector": "src/melee/lb/lbvector.c",
    "accessory": "src/melee/ft/ft_0D4D.c",
    "position_consumer": "src/melee/ft/fighter.c",
    "motion_types": "src/melee/ft/kinds/ftCommon/types.h",
}
ABI_HEADERS = {
    "fighter_forward": (
        "src/melee/ft/forward.h",
        "73cf4918e768aa15477d7c9e17d6fa58ded77ee60c9e0398820dda1ac6cfc7fe",
    ),
    "hsd_gobj": (
        "src/sysdolphin/baselib/gobj.h",
        "22edd67f12be7c47abd78c78c1a9e0c27f288cedcde9338e6aa4c75195b517a2",
    ),
}


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _file_sha(path):
    return _sha(path.read_bytes())


def _matching_brace(source, opening):
    """Return an exclusive brace end while ignoring comments and literals."""
    if source[opening] != "{":
        raise AssertionError("brace scan did not start at an opening brace")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "block":
            if char == "*" and next_char == "/":
                state = "code"
                index += 1
        elif state == "string":
            if char == "\\":
                index += 1
            elif char == '"':
                state = "code"
        elif state == "char":
            if char == "\\":
                index += 1
            elif char == "'":
                state = "code"
        elif char == "/" and next_char == "/":
            state = "line"
            index += 1
        elif char == "/" and next_char == "*":
            state = "block"
            index += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "char"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise AssertionError("unterminated source brace block")


def _function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    return source[start:_matching_brace(source, opening)] + "\n"


def _case_fragment(function, case_label, next_case_label=None):
    switch_anchor = "switch (fp->mv.co.unk_deadup.x44) {"
    # The animation routine has an earlier one-case switch over this same
    # field; case 2 belongs to the later timer-dispatch switch.
    switch_start = function.rindex(switch_anchor)
    switch_open = function.index("{", switch_start)
    switch_end = _matching_brace(function, switch_open)
    case_start = function.index(case_label, switch_open, switch_end)
    if next_case_label is not None:
        case_end = function.index(next_case_label, case_start, switch_end)
    else:
        case_end = switch_end - 1
    fragment = function[case_start:case_end]
    if not fragment.lstrip().startswith(case_label):
        raise AssertionError(f"source case extraction missed {case_label!r}")
    return fragment.rstrip() + "\n"


def _timer_decrement(function):
    marker = "if (fp->mv.co.unk_deadup.x40 != 0)"
    if function.count(marker) != 1:
        raise AssertionError("expected one source DeadUpFall timer decrement")
    start = function.index(marker)
    opening = function.index("{", start)
    return function[start:_matching_brace(function, opening)] + "\n"


def _position_block(fighter_source):
    signature = "void Fighter_8006A360(Fighter_GObj* gobj)"
    function = _function(fighter_source, signature)
    marker = "    if (!fp->x221F_b3) {\n        fp->pos_delta.x = fp->cur_pos.x - fp->prev_pos.x;"
    if function.count(marker) != 1:
        raise AssertionError("next-tick cur_pos consumer excerpt changed")
    start = function.index(marker)
    last = "        fp->prev_pos = fp->cur_pos;"
    end = function.index(last, start) + len(last)
    block = function[start:end] + "\n    }\n"
    for required in ("pos_delta.x", "pos_delta.y", "pos_delta.z", "prev_pos = fp->cur_pos"):
        if block.count(required) != 1:
            raise AssertionError(f"source position consumer lost {required!r}")
    return block


def _git(repo, *args):
    result = subprocess.run(
        ["git", "-C", str(repo), *args], cwd=ROOT,
        capture_output=True, text=True, check=False,
    )
    if result.returncode:
        raise RuntimeError(
            f"git {args!r} failed ({result.returncode}): "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
    return result


def _materialize_sources(output):
    actual_pin = subprocess.check_output(
        ["git", "-C", str(MELEE), "rev-parse", "HEAD"], cwd=ROOT, text=True,
    ).strip()
    if actual_pin != PIN:
        raise AssertionError(f"unsupported Melee source commit {actual_pin}; expected {PIN}")

    for relative in (*SOURCE_PATHS.values(), *(path for path, _ in ABI_HEADERS.values())):
        changed = subprocess.check_output(
            ["git", "-C", str(MELEE), "status", "--porcelain", "--", relative],
            cwd=ROOT, text=True,
        )
        if changed:
            raise AssertionError(f"pinned source is locally modified at {relative}: {changed!r}")

    repo = output / "source-composition"
    repo.mkdir()
    copied = {}
    for name, relative in SOURCE_PATHS.items():
        source = MELEE / relative
        destination = repo / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        copied[name] = destination
    abi_header_hashes = {}
    for name, (relative, expected_hash) in ABI_HEADERS.items():
        header_hash = _file_sha(MELEE / relative)
        if header_hash != expected_hash:
            raise AssertionError(
                f"unsupported pinned ABI header {relative}: {header_hash}; "
                f"expected {expected_hash}"
            )
        abi_header_hashes[name] = header_hash
    _git(repo, "init", "-q")

    targets = tuple(SOURCE_PATHS.values())
    include_args = [argument for target in targets for argument in (f"--include={target}",)]
    _git(repo, "apply", "--check", *include_args, str(BASE_PATCH))
    _git(repo, "apply", *include_args, str(BASE_PATCH))

    original_anim_source = copied["anim_physics"].read_text(encoding="utf-8")
    original_draw_source = copied["draw"].read_text(encoding="utf-8")
    profile_targets = (SOURCE_PATHS["anim_physics"], SOURCE_PATHS["draw"])
    profile_include_args = [
        argument for target in profile_targets for argument in (f"--include={target}",)
    ]
    _git(repo, "apply", "--check", *profile_include_args, str(PROFILE_PATCH))
    _git(repo, "apply", *profile_include_args, str(PROFILE_PATCH))
    profile_anim_source = copied["anim_physics"].read_text(encoding="utf-8")
    profile_draw_source = copied["draw"].read_text(encoding="utf-8")

    original_anim = _function(original_anim_source, "void ftCo_DeadUpFall_Anim(Fighter_GObj* gobj)")
    profile_anim = _function(profile_anim_source, "void ftCo_DeadUpFall_Anim(Fighter_GObj* gobj)")
    original_phys = _function(original_anim_source, "void ftCo_DeadUpFall_Phys(Fighter_GObj* gobj)")
    profile_phys = _function(profile_anim_source, "void ftCo_DeadUpFall_Phys(Fighter_GObj* gobj)")
    original_draw = _function(
        original_draw_source,
        "static inline void ftDrawCommon_80080E18_inline2(HSD_GObj* gobj, Fighter* old)",
    )
    profile_draw = _function(
        profile_draw_source,
        "static inline void ftDrawCommon_80080E18_inline2(HSD_GObj* gobj, Fighter* old)",
    )
    original_driver = _function(
        original_draw_source, "void ftDrawCommon_80080E18(HSD_GObj* gobj, int arg1)",
    )
    profile_driver = _function(
        profile_draw_source, "void ftDrawCommon_80080E18(HSD_GObj* gobj, int arg1)",
    )
    if original_driver != profile_driver:
        raise AssertionError("the profile overlay unexpectedly changed the draw driver")

    timer = _timer_decrement(original_anim)
    case_init_original = _case_fragment(original_anim, "case 2:", "case 3:")
    case_init_profile = _case_fragment(profile_anim, "case 2:", "case 3:")
    case_phys_original = _case_fragment(original_phys, "case 3:")
    case_phys_profile = _case_fragment(profile_phys, "case 3:")
    if "slippi_profile_deadupfall_initialize" not in case_init_profile:
        raise AssertionError("profile phase-2 hook missing from extracted actual source")
    if "slippi_profile_deadupfall_advance" not in case_phys_profile:
        raise AssertionError("profile case-3 hook missing from extracted actual source")
    if "ftCommon_Fall(fp" not in case_phys_original or "lbVector_Add(&fp->mv.co.unk_deadup.x5C" not in case_phys_original:
        raise AssertionError("original case-3 source dependency changed")
    if "&fp->cur_pos" not in original_draw or "Vec3 model_pos;" not in profile_draw:
        raise AssertionError("source draw destination split changed")
    if "PSMTXMultVec(matrix, (Vec3*) &old->mv.co.walk.fast_anim_frame," not in profile_draw:
        raise AssertionError("source model matrix input changed")

    fall = _function(
        copied["fall"].read_text(encoding="utf-8"),
        "void ftCommon_Fall(Fighter* fp, float gravity, float terminal_vel)",
    )
    vector_add = _function(
        copied["vector"].read_text(encoding="utf-8"), "Vec3* lbVector_Add(Vec3* a, Vec3* b)",
    )
    matrix = _function(
        (ROOT / "src/gameplay_ps_math.c").read_text(encoding="utf-8"),
        "void melee_web_ps_mtx_mult_vec(",
    )
    if "fmaf(m[row][2], z, m[row][0] * x)" not in matrix:
        raise AssertionError("actual paired-single matrix function body changed")
    matrix = matrix.replace(
        "void melee_web_ps_mtx_mult_vec(",
        "void extracted_melee_web_ps_mtx_mult_vec(", 1,
    )
    accessory = _function(
        copied["accessory"].read_text(encoding="utf-8"),
        "void fn_800D4DD4(Fighter_GObj* gobj)",
    )
    position = _position_block(copied["position_consumer"].read_text(encoding="utf-8"))
    compat = (ROOT / "src/gameplay_compat.h").read_text(encoding="utf-8")
    compat_binding = "#define PSMTXMultVec melee_web_ps_mtx_mult_vec"
    if compat.count(compat_binding) != 1:
        raise AssertionError("the existing PSMTXMultVec source binding changed")

    return {
        "original_anim": original_anim,
        "profile_anim": profile_anim,
        "original_phys": original_phys,
        "profile_phys": profile_phys,
        "original_draw": original_draw,
        "profile_draw": profile_draw,
        "draw_driver": original_driver,
        "timer": timer,
        "anim_case_original": case_init_original,
        "anim_case_profile": case_init_profile,
        "physics_case_original": case_phys_original,
        "physics_case_profile": case_phys_profile,
        "fall": fall,
        "vector_add": vector_add,
        "matrix": matrix,
        "accessory": accessory,
        "position": position,
        "identity": {
            "melee_source_commit": actual_pin,
            "abi_header_sha256": abi_header_hashes,
            "source_file_sha256": {
                name: _file_sha(MELEE / relative)
                for name, relative in SOURCE_PATHS.items()
            },
            "base_patch_sha256": _file_sha(BASE_PATCH),
            "profile_overlay_sha256": _file_sha(PROFILE_PATCH),
            "deadupfall_helper_sha256": _file_sha(ROOT / "src/slippi_profile/deadupfall_fix.c"),
            "deadupfall_header_sha256": _file_sha(ROOT / "src/slippi_profile/deadupfall_fix.h"),
            "profile_identity_sha256": _file_sha(ROOT / "src/slippi_profile/profile.c"),
            "gameplay_ps_math_sha256": _file_sha(ROOT / "src/gameplay_ps_math.c"),
            "gameplay_compat_sha256": _file_sha(ROOT / "src/gameplay_compat.h"),
            "compatibility_binding": compat_binding,
            "extracted_sha256": {
                "timer_decrement": _sha(timer.encode()),
                "anim_case_original": _sha(case_init_original.encode()),
                "anim_case_profile": _sha(case_init_profile.encode()),
                "physics_case_original": _sha(case_phys_original.encode()),
                "physics_case_profile": _sha(case_phys_profile.encode()),
                "draw_helper_original": _sha(original_draw.encode()),
                "draw_helper_profile": _sha(profile_draw.encode()),
                "draw_driver": _sha(original_driver.encode()),
                "ftCommon_Fall": _sha(fall.encode()),
                "lbVector_Add": _sha(vector_add.encode()),
                "matrix_function": _sha(matrix.encode()),
                "accessory": _sha(accessory.encode()),
                "position_consumer": _sha(position.encode()),
            },
        },
    }


def _rename_draw_helper(source, helper_name):
    old_helper = "static inline void ftDrawCommon_80080E18_inline2("
    if source.count(old_helper) != 1:
        raise AssertionError("draw helper renaming anchor changed")
    return source.replace(old_helper, f"static inline void {helper_name}(", 1)


def _rename_draw_driver(source, helper_name, driver_name):
    old_driver = "void ftDrawCommon_80080E18(HSD_GObj* gobj, int arg1)"
    if source.count(old_driver) != 1:
        raise AssertionError("draw driver renaming anchor changed")
    source = source.replace(old_driver, f"void {driver_name}(HSD_GObj* gobj, int arg1)", 1)
    old_call = "ftDrawCommon_80080E18_inline2(gobj, fp);"
    if source.count(old_call) != 1:
        raise AssertionError("draw driver helper call anchor changed")
    return source.replace(old_call, f"{helper_name}(gobj, fp);", 1)


def _build_fixture(output, sources, mutation=None, *, generation_only=False):
    template = FIXTURE.read_text(encoding="utf-8")
    selected_anim = sources["anim_case_profile"]
    selected_physics = sources["physics_case_profile"]
    selected_draw = sources["profile_draw"]
    if mutation == "omit_init":
        selected_anim = sources["anim_case_original"]
    elif mutation == "omit_physics":
        selected_physics = sources["physics_case_original"]
    elif mutation == "omit_model":
        selected_draw = sources["original_draw"]

    original_helper = _rename_draw_helper(
        sources["original_draw"], "fixture_original_inline2",
    )
    profile_helper = _rename_draw_helper(selected_draw, "fixture_profile_inline2")
    original_driver = _rename_draw_driver(
        sources["draw_driver"], "fixture_original_inline2", "fixture_draw_original",
    )
    profile_driver = _rename_draw_driver(
        sources["draw_driver"], "fixture_profile_inline2", "fixture_draw_profile",
    )
    replacements = {
        "@ACTUAL_FALL@": sources["fall"],
        "@ACTUAL_VECTOR_ADD@": sources["vector_add"],
        "@ACTUAL_MATRIX_FUNCTION@": sources["matrix"],
        "@TIMER_DECREMENT_ORIGINAL@": sources["timer"],
        "@TIMER_DECREMENT_PROFILE@": sources["timer"],
        "@ANIM_CASE_ORIGINAL@": sources["anim_case_original"],
        "@ANIM_CASE_PROFILE@": selected_anim,
        "@PHYSICS_CASE_ORIGINAL@": sources["physics_case_original"],
        "@PHYSICS_CASE_PROFILE@": selected_physics,
        "@DRAW_HELPER_ORIGINAL@": original_helper,
        "@DRAW_HELPER_PROFILE@": profile_helper,
        "@DRAW_DRIVER_ORIGINAL@": original_driver,
        "@DRAW_DRIVER_PROFILE@": profile_driver,
        "@ACTUAL_ACCESSORY@": sources["accessory"],
        "@POSITION_BLOCK@": sources["position"],
    }
    fixture_text = template
    for token, replacement in replacements.items():
        if fixture_text.count(token) != 1:
            raise AssertionError(f"fixture token mismatch for {token}: {fixture_text.count(token)}")
        fixture_text = fixture_text.replace(token, replacement, 1)
    if re.search(r"@[A-Z][A-Z0-9_]*@", fixture_text):
        raise AssertionError("unexpanded fixture token remains")
    fixture_source = output / (f"fixture-{mutation or 'all-hooks'}.c")
    fixture_source.write_text(fixture_text, encoding="utf-8")
    if generation_only:
        return fixture_source, _sha(fixture_text.encode())
    executable = output / f"deadupfall-{mutation or 'all-hooks'}"
    command = [
        *shlex.split(os.environ.get("CC", "cc")),
        "-std=c11", "-DTARGET_PC", "-DMELEE_WEB_SLIPPI_PROFILE=1",
        "-Wall", "-Wextra", "-Werror", "-ffp-contract=off", "-fno-fast-math",
        "-include", str(ROOT / "src/gameplay_compat.h"),
        "-I", str(ROOT / "src"),
        "-I", str(ROOT / ".deps/aurora/include"),
        "-I", str(ROOT / ".deps/melee/src"),
        "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
        str(fixture_source),
        str(ROOT / "src/slippi_profile/profile.c"),
        str(ROOT / "src/slippi_profile/deadupfall_fix.c"),
        "-lm", "-o", str(executable),
    ]
    wrapped = [
        os.environ.get("PYTHON", "python3"),
        str(ROOT / "scripts/agent_workspace.py"), "run", "--", *command,
    ]
    compile_result = subprocess.run(
        wrapped, cwd=ROOT, capture_output=True, text=True, check=False,
    )
    (output / f"compile-{mutation or 'all-hooks'}.stdout").write_text(
        compile_result.stdout, encoding="utf-8",
    )
    (output / f"compile-{mutation or 'all-hooks'}.stderr").write_text(
        compile_result.stderr, encoding="utf-8",
    )
    if compile_result.returncode:
        raise AssertionError(
            f"native source fixture compile failed for {mutation}: "
            f"{compile_result.stdout}\n{compile_result.stderr}"
        )
    return executable, _sha(fixture_text.encode())


class DeadUpFallReductionTests(unittest.TestCase):
    def test_actual_source_v2_hooks_match_and_each_omission_is_detected(self):
        work_root = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work")).resolve()
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="slippi-deadupfall-v2-", dir=work_root))
        retain_output = os.environ.get("MELEE_WEB_RETAIN_OUTPUT") == "1"
        try:
            sources = _materialize_sources(output)
            (output / "source_identity.json").write_text(
                json.dumps(sources["identity"], indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            variants = {}
            for mutation in (None, "omit_init", "omit_physics", "omit_model"):
                executable, fixture_hash = _build_fixture(output, sources, mutation)
                run = subprocess.run(
                    [str(executable)], cwd=ROOT, capture_output=True, text=True,
                    check=False,
                )
                name = mutation or "all-hooks"
                (output / f"run-{name}.stdout").write_text(run.stdout, encoding="utf-8")
                (output / f"run-{name}.stderr").write_text(run.stderr, encoding="utf-8")
                variants[name] = {
                    "fixture_sha256": fixture_hash,
                    "exit_code": run.returncode,
                    "stdout": run.stdout,
                    "stderr": run.stderr,
                }
                if mutation is None:
                    self.assertEqual(
                        run.returncode, 0,
                        f"all-hook source fixture failed; retained: {output}",
                    )
                    self.assertIn("DeadUpFall actual-source v2 reduction passed", run.stdout)
                    variants[name]["classification"] = "positive_pass"
                else:
                    expected_marker = {
                        "omit_init": "EXPECTED_FAILURE:deadupfall_init_private_velocity",
                        "omit_physics": "EXPECTED_FAILURE:deadupfall_physics_private_update",
                        "omit_model": "EXPECTED_FAILURE:deadupfall_model_draw_is_simulation_pure",
                    }[mutation]
                    self.assertEqual(
                        run.returncode, 86,
                        f"{mutation} did not stop at its controlled invariant; "
                        f"retained: {output}\nstdout={run.stdout}\nstderr={run.stderr}",
                    )
                    self.assertIn("ORIGINAL_SOURCE_CONTROLS_PASSED", run.stdout)
                    self.assertIn("PROFILE_GREEN_ENTRY_REACHED", run.stdout)
                    self.assertIn(expected_marker, run.stderr)
                    variants[name]["classification"] = "expected_omission_invariant"
                    variants[name]["expected_marker"] = expected_marker
            evidence = {
                "scope": "native actual-source reduction; not whole-game validation",
                "fixture_identity": sources["identity"],
                "variants": variants,
                "controls": [
                    "original draw plus next-tick position/accessory consumer is a nonidentity-view causal red",
                    "identity-view original-source control stays unchanged",
                    "profile present draw and correction no-draw compare declared motion/velocity/position bytes",
                    "actual source draw driver guards, repeated draws, changed view, timer/phase/clamp/early-break order",
                    "independent omitted phase-2 init, case-3 physics and model-position hooks are detected",
                    "native fixture service adapters are synthetic, bounded observers; unexpected render helpers abort",
                    "reduced phase coverage extracts actual case-2 initialization and case-3 physics only; its phase-1 no-dispatch control does not establish full phase-1 behavior",
                ],
            }
            (output / "result.json").write_text(
                json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8",
            )
        except Exception as error:
            (output / "failure.txt").write_text(f"{error!r}\n", encoding="utf-8")
            self.fail(f"DeadUpFall source reduction failed: {error}; retained: {output}")
        else:
            if retain_output:
                print(f"retained DeadUpFall reduction evidence: {output}")
            else:
                shutil.rmtree(output)


if __name__ == "__main__":
    unittest.main()
