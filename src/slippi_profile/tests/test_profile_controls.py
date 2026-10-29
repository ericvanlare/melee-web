# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused native controls for the GPL-3.0-or-later Slippi profile helpers."""

import os
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
MELEE = ROOT / ".deps/melee"
MELEE_SOURCE_PIN = "b43912cc78606f96c9569f5d6229bc9d7e265ea5"


def _simple_source_function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError(f"expected one pinned source function {signature!r}")
    start = source.index(signature)
    opening = source.index("{", start)
    end = source.index("\n}", opening) + 2
    function = source[start:end]
    if function[opening - start + 1:-1].count("{") != 0:
        raise AssertionError(f"source function gained nested control flow: {signature!r}")
    return function + "\n"


def _source_statement(source, marker, label):
    if source.count(marker) != 1:
        raise AssertionError(f"expected one source {label} statement")
    start = source.index(marker)
    end = source.index(";", start) + 1
    return source[start:end]


def _source_function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError(f"expected one source function {signature!r}")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1] + "\n"
    raise AssertionError(f"unterminated source function {signature!r}")


class ProfileControlsTests(unittest.TestCase):
    def test_profile_policy_and_dependency_controls(self):
        work_root = ROOT / "work"
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="slippi-profile-controls-", dir=work_root))
        executable = output / "profile-controls"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        command = [
            *compiler,
            "-std=c11",
            "-DTARGET_PC",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I", str(ROOT / "src/slippi_profile/tests/stubs"),
            "-I", str(ROOT / "src"),
            "-I", str(ROOT / ".deps/melee/src"),
            "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
            "-include", str(ROOT / "src/gameplay_compat.h"),
            str(ROOT / "src/slippi_profile/profile.c"),
            str(ROOT / "src/slippi_profile/offscreen_damage.c"),
            str(ROOT / "src/slippi_profile/dynamics_fix.c"),
            str(ROOT / "src/slippi_profile/deadupfall_fix.c"),
            str(ROOT / "src/slippi_profile/draw_diagnostics.c"),
            str(ROOT / "src/slippi_profile/whispy_direction_fix.c"),
            str(ROOT / "src/slippi_profile/ps_camera_independent_monitor_fix.c"),
            str(ROOT / "src/slippi_profile/ucf_dbooc_fix.c"),
            str(ROOT / "src/slippi_profile/tests/profile_controls_test.c"),
            "-lm",
            "-o", str(executable),
        ]
        wrapped = [
            os.environ.get("PYTHON", "python3"),
            str(ROOT / "scripts/agent_workspace.py"),
            "run", "--", *command,
        ]
        v10_command = list(command)
        v10_command.insert(v10_command.index("-o"),
                           "-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1")
        v10_command[v10_command.index("-o") + 1] = str(output / "profile-controls-v10")
        wrapped_v10 = [
            os.environ.get("PYTHON", "python3"),
            str(ROOT / "scripts/agent_workspace.py"),
            "run", "--", *v10_command,
        ]
        try:
            subprocess.run(wrapped, cwd=ROOT, check=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True)
            subprocess.run(wrapped_v10, cwd=ROOT, check=True)
            subprocess.run([str(output / "profile-controls-v10")], cwd=ROOT, check=True)
        except Exception as error:
            self.fail(f"Profile controls failed: {error}; retained reproducer directory: {output}")
        else:
            shutil.rmtree(output)

    def test_actual_ground_proc_refreshes_after_authored_callback(self):
        work_root = ROOT / "work"
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(
            prefix="ground-matrix-source-control-", dir=work_root))
        source_tree = output / "source"
        ground_path = source_tree / "src/melee/gr/ground.c"
        ground_path.parent.mkdir(parents=True)
        subprocess.run(["git", "init", "-q"], cwd=source_tree, check=True)
        actual_pin = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=MELEE, text=True).strip()
        self.assertEqual(actual_pin, MELEE_SOURCE_PIN)
        ground_source = subprocess.check_output(
            ["git", "show", f"{MELEE_SOURCE_PIN}:src/melee/gr/ground.c"],
            cwd=MELEE, text=True,
        )
        ground_path.write_text(ground_source, encoding="utf-8")

        try:
            for patch_path in (
                ROOT / "patches/melee-gameplay.patch",
                ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch",
            ):
                subprocess.run([
                    "git", "apply", "--check",
                    "--include=src/melee/gr/ground.c", str(patch_path),
                ], cwd=source_tree, check=True)
                subprocess.run([
                    "git", "apply",
                    "--include=src/melee/gr/ground.c", str(patch_path),
                ], cwd=source_tree, check=True)

            composed_ground = ground_path.read_text(encoding="utf-8")
            function = _source_function(
                composed_ground, "static void Ground_801C1D38(HSD_GObj* gobj)")
            template = (ROOT / "src/slippi_profile/tests/"
                        "ground_matrix_refresh_control.c.in").read_text(
                            encoding="utf-8")
            self.assertEqual(template.count("GROUND_FUNCTION"), 1)
            fixture = output / "ground-control.c"
            fixture.write_text(template.replace("GROUND_FUNCTION", function),
                               encoding="utf-8")
            common = [
                *shlex.split(os.environ.get("CC", "cc")),
                "-std=c11", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror",
                "-DMELEE_WEB_SLIPPI_PROFILE=1",
                "-I", str(ROOT / "src/slippi_profile/tests/stubs"),
                "-I", str(ROOT / "src"),
                "-I", str(MELEE / "src"),
                "-I", str(ROOT / ".deps/aurora/include"),
                "-I", str(MELEE / "extern/dolphin/include"),
                "-include", str(ROOT / "src/gameplay_compat.h"),
                str(ROOT / "src/slippi_profile/dynamics_fix.c"),
                str(fixture),
            ]
            for lane, extra_defines in (
                ("v10", ["-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1"]),
                ("legacy", []),
            ):
                executable = output / ("ground-control-" + lane)
                command = [*common, *extra_defines, "-o", str(executable)]
                wrapped = [
                    os.environ.get("PYTHON", "python3"),
                    str(ROOT / "scripts/agent_workspace.py"),
                    "run", "--", *command,
                ]
                subprocess.run(wrapped, cwd=ROOT, check=True)
                subprocess.run([str(executable)], cwd=ROOT, check=True)
        except Exception as error:
            self.fail(
                f"Ground source control failed: {error}; retained reproducer: {output}")
        else:
            shutil.rmtree(output)

    def test_actual_source_player_id_and_raw_pad_mapping(self):
        upstream_paths = (
            "src/melee/pl/player.c",
            "src/melee/ft/fighter.c",
        )
        actual_pin = subprocess.check_output(
            ["git", "-C", str(MELEE), "rev-parse", "HEAD"], cwd=ROOT, text=True,
        ).strip()
        if actual_pin != MELEE_SOURCE_PIN:
            self.fail(f"unsupported source commit {actual_pin}; expected {MELEE_SOURCE_PIN}")
        for relative in upstream_paths:
            changed = subprocess.check_output(
                ["git", "-C", str(MELEE), "status", "--porcelain", "--", relative],
                cwd=ROOT, text=True,
            )
            if changed:
                self.fail(f"pinned upstream source is locally modified at {relative}: {changed!r}")

        player_source = (MELEE / upstream_paths[0]).read_text(encoding="utf-8")
        fighter_source = (MELEE / upstream_paths[1]).read_text(encoding="utf-8")
        player_context_source = (ROOT / "src/gameplay_player_context.c").read_text(encoding="utf-8")
        match_context_source = (ROOT / "src/gameplay_match_context.c").read_text(encoding="utf-8")
        snapshot_source = (ROOT / "tests/gameplay_snapshot_probe.cpp").read_text(encoding="utf-8")
        match_session_source = (ROOT / "src/gameplay_match_session.cpp").read_text(encoding="utf-8")

        accessors = "\n".join(_simple_source_function(player_source, signature) for signature in (
            "u8 Player_GetControllerIndex(int slot)",
            "void Player_SetControllerIndex(int slot, s8 controller_index)",
            "int Player_GetPlayerId(int slot)",
            "void Player_SetPlayerId(int slot, int player_id)",
        ))
        context_lines = "\n".join(_source_statement(player_context_source, marker, label) for marker, label in (
            ("Player_SetControllerIndex(s->slot,s->sub_color)", "tint assignment"),
            ("Player_SetPlayerId(s->slot,s->slot)", "player-id assignment"),
        ))
        fighter_lines = "\n".join(_source_statement(fighter_source, marker, label) for marker, label in (
            ("fp->player_id = argdata->slot", "fighter source-slot assignment"),
            ("fp->x618_player_id = Player_GetPlayerId(fp->player_id)", "fighter processed-PAD assignment"),
            ("fp->x61A_controller_index = Player_GetControllerIndex(fp->player_id)", "fighter tint assignment"),
        ))
        queue_line = _source_statement(
            match_context_source,
            "h->input_queue.stat[h->slots[i]]=raw[h->controllers[i]]",
            "raw-to-processed queue mapping",
        )
        human_read = _source_statement(
            fighter_source,
            "HSD_PadGameStatus[fp->x618_player_id].button",
            "human PAD read",
        )
        human_assignment_start = fighter_source.rfind("fp->input.held_buttons[0] =", 0,
            fighter_source.index(human_read))
        if human_assignment_start < 0:
            self.fail("human PAD read is no longer assigned to held_buttons[0]")
        human_assignment = fighter_source[human_assignment_start:
            fighter_source.index(";", human_assignment_start) + 1]

        copied_formula_assignment = _source_statement(
            snapshot_source,
            "initializer_diagnostic.controllers[slot]=source.slot?source.slot-1u:slot",
            "copied-start raw-controller formula",
        )
        session_formula_assignment = _source_statement(
            match_session_source,
            "const unsigned source_port=source.slot?source.slot-1u:source_slot",
            "match source-port formula",
        )
        session_guard_marker = "check(source_port==source_slot,"
        if match_session_source.count(session_guard_marker) != 1:
            self.fail("match entry no longer enforces copied source-port identity")
        copied_formula = copied_formula_assignment.split("=", 1)[1].rstrip(";")
        session_formula = session_formula_assignment.split("=", 1)[1].rstrip(";")

        template = (ROOT / "src/slippi_profile/tests/draw_input_mapping_fixture.c.in").read_text(
            encoding="utf-8")
        generated = template
        for key, value in {
            "PLAYER_ACCESSORS": accessors,
            "CONTEXT_ASSIGNMENTS": context_lines,
            "FIGHTER_ASSIGNMENTS": fighter_lines,
            "HUMAN_BUTTON_READ": human_assignment,
            "RAW_TO_PROCESSED_QUEUE": queue_line,
            "COPIED_START_FORMULA": copied_formula,
            "SESSION_SOURCE_PORT_FORMULA": session_formula,
            "SESSION_IDENTITY_GUARD": session_guard_marker[len("check("):-1],
        }.items():
            token = "{{" + key + "}}"
            if generated.count(token) != 1:
                self.fail(f"source reduction token cardinality changed: {token}")
            generated = generated.replace(token, value, 1)
        if re.search(r"\{\{[A-Z][A-Z0-9_]*\}\}", generated):
            self.fail("source reduction left an unfilled fixture marker")

        work_root = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work")).resolve()
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="slippi-input-mapping-source-", dir=work_root))
        source_file = output / "draw_input_mapping_fixture.c"
        executable = output / "draw-input-mapping"
        source_file.write_text(generated, encoding="utf-8")
        compiler = shlex.split(os.environ.get("CC", "cc"))
        command = [*compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
            str(source_file), "-o", str(executable)]
        wrapped = [os.environ.get("PYTHON", "python3"),
            str(ROOT / "scripts/agent_workspace.py"), "run", "--", *command]
        source_paths = [MELEE / path for path in upstream_paths] + [
            ROOT / path for path in ("src/gameplay_player_context.c",
                "src/gameplay_match_context.c", "src/gameplay_match_session.cpp",
                "tests/gameplay_snapshot_probe.cpp")]
        (output / "source_identity.json").write_text(json.dumps({
            "upstream_pin": actual_pin,
            "source_files": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                             for path in source_paths},
            "generated_sha256": hashlib.sha256(generated.encode()).hexdigest(),
            "scope": "actual-source snippets with synthetic storage/queue services; not full PAD or live gameplay",
        }, indent=2) + "\n")
        try:
            compiled = subprocess.run(wrapped, cwd=ROOT, text=True, capture_output=True)
            (output / "compile.log").write_text(compiled.stdout + compiled.stderr)
            compiled.check_returncode()
            run = subprocess.run([str(executable)], cwd=ROOT, text=True, capture_output=True)
            (output / "run.log").write_text(run.stdout + run.stderr)
            run.check_returncode()
            (output / "result.json").write_text(json.dumps({
                "compile_exit": compiled.returncode, "run_exit": run.returncode,
                "command": command, "scope": "synthetic actual-source mapping reduction",
            }, indent=2) + "\n")
        except Exception as error:
            self.fail(f"Actual-source input mapping reduction failed: {error}; retained reproducer directory: {output}")
        else:
            if os.environ.get("MELEE_WEB_RETAIN_OUTPUT") == "1":
                print(f"Retained actual-source mapping evidence: {output}")
            else:
                shutil.rmtree(output)



if __name__ == "__main__":
    unittest.main()
