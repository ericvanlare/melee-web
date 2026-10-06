"""Compile the Team Battle admission rule and the patched match validator."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches/melee-gameplay.patch"
VALIDATOR = "melee_web_match_validate_source_start"


def patched_validator_source():
    """Return the validator text added by the canonical gameplay patch."""
    lines = PATCH.read_text(encoding="utf-8").splitlines()
    start = next(
        index for index, line in enumerate(lines)
        if line.startswith(f"+static int {VALIDATOR}("))
    body = []
    for line in lines[start:]:
        if not line.startswith("+"):
            raise AssertionError("validator hunk is not one contiguous addition")
        body.append(line[1:])
        if line == "+}":
            return "\n".join(body) + "\n"
    raise AssertionError("validator end not found in patch")


class GameplayTeamSetupTests(unittest.TestCase):
    def compile_and_run(self, extra_flags=()):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("a C compiler is required")
        if not (ROOT / "build/gameplay-source/src/melee/gm/types.h").is_file():
            self.skipTest("Prepared gameplay source unavailable; build gameplay first")
        with tempfile.TemporaryDirectory(prefix="melee team setup ") as directory:
            work = Path(directory)
            validator = work / "patched_validator.inc"
            validator.write_text(patched_validator_source(), encoding="utf-8")
            output = work / "gameplay_team_setup_trace"
            command = [
                compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-variable", "-DAURORA", "-DTARGET_PC",
                "-DMELEE_WEB_GAMEPLAY",
                f'-DMELEE_WEB_PATCHED_VALIDATOR="{validator}"',
                *extra_flags,
                "-I", str(ROOT / "src"),
                "-I", str(ROOT / "build/gameplay-source/src"),
                "-I", str(ROOT / ".deps/aurora/include"),
                "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                "-O1", "-ffunction-sections", "-fdata-sections",
                "-ffp-contract=off",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                str(ROOT / "tests/gameplay_team_setup_trace.c"),
                "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                "-o", str(output),
            ]
            result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(output)], cwd=ROOT, capture_output=True,
                                 text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            return run.stdout

    def test_validators_admit_exactly_the_source_css_team_setups(self):
        self.assertIn("admit original CSS team setups", self.compile_and_run())

    def test_patch_has_one_shared_team_rule_and_no_two_player_bound(self):
        text = patched_validator_source()
        self.assertIn("melee_web_team_setup_supported(start, i, 1)", text)
        # The former inline two-player bound must not return.
        self.assertNotIn("i != 2", text)
        self.assertNotIn("players[1].team", text)
        self.assertNotIn("player->team > 2", text)


if __name__ == "__main__":
    unittest.main()
