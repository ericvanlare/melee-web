"""Focused terminal-cursor control against original GM source bodies.

The stock values and completed-bootstrap increments below are explicitly
modeled component leaves. This does not claim real PAD-to-KO or browser proof.
"""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(os.environ.get("MELEE_TERMINAL_FRONTIER_REPO_ROOT",
                          Path(__file__).resolve().parents[1])).resolve()
SOURCE_ROOT = Path(os.environ.get("MELEE_TERMINAL_FRONTIER_SOURCE_ROOT",
                                 Path(__file__).resolve().parents[1])).resolve()
UPSTREAM_ROOT = Path(os.environ.get("MELEE_TERMINAL_FRONTIER_UPSTREAM_ROOT",
                                   ROOT)).resolve()
import sys
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


def extract_function(source: str, name: str) -> str:
    """Extract one actual C definition, ignoring braces in comments/strings."""
    import re
    matches = list(re.finditer(r"\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", source,
                               flags=re.S))
    if not matches:
        raise AssertionError(f"actual upstream definition not found: {name}")
    match = matches[-1]
    opening = source.find("{", match.start(), match.end())
    depth = 0
    state = "code"
    escaped = False
    index = opening
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and following == "/":
                state = "code"
                index += 1
        elif state in ("string", "char"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"
        else:
            if char == "/" and following == "/":
                state = "line_comment"
                index += 1
            elif char == "/" and following == "*":
                state = "block_comment"
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
                    # Keep the declaration prefix (`static inline void`,
                    # `MatchOutcome`, etc.) on the definition line. The
                    # reduced fixture compiles the original body, so stripping
                    # its return type silently changes the source function.
                    line_start = source.rfind("\n", 0, match.start()) + 1
                    return source[line_start:index + 1]
        index += 1
    raise AssertionError(f"unterminated upstream definition: {name}")


class RuntimeMatchHistoryTerminalCursorTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "runtime-terminal-cursor-")

    def test_original_stock_transition_and_cursor_component(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("a C compiler is required")
        original = (UPSTREAM_ROOT / ".deps/melee/src/melee/gm/gm_16AE.c").read_text()
        names = {
            "FN_INLINE": "fn_8016CFE0_inline",
            "FN_FFA": "gm_GetFFAOutcome",
            "FN_OUTCOME": "gm_GetMatchOutcome",
            "FN_CD98": "fn_8016CD98",
            "FN_CFE0": "fn_8016CFE0",
            "FN_SCENE": "gm_Scene_Vs_OnFrame",
        }
        template = (SOURCE_ROOT / "tests/runtime_match_history_terminal_cursor_control.c.in").read_text()
        for marker, name in names.items():
            template = template.replace("@" + marker + "@", extract_function(original, name))
        if "@FN_" in template:
            self.fail("an actual original GM function was not extracted")
        source = self.work / "terminal-cursor-control.c"
        binary = self.work / "terminal-cursor-control"
        log = self.work / "terminal-cursor-control.log"
        source.write_text(template)
        command = [compiler, "-std=c11", "-O0", "-Wno-unused-parameter",
                   "-Wno-unused-variable", "-Wno-unused-function",
                   "-Wno-implicit-function-declaration",
                   "-I", str(SOURCE_ROOT / "src"), str(source), "-o", str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        log.write_text("$ " + " ".join(command) + "\n" + result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        with log.open("a") as stream:
            stream.write("\n$ " + str(binary) + "\n" + run.stdout + run.stderr)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("component control", run.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
