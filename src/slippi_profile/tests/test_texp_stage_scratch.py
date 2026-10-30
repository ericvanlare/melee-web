# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the actual TExp/TEV source bodies with a modeled synchronous GX sink."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / "scripts"), str(ROOT / "tests")]
from check_gameplay import node_runtime
from gameplay_bool import composed_patch
from gameplay_sources import compose_slippi_profile_patch
from owned_test_workspace import OwnedWorkspaceTests


def function(text, signature):
    match = re.search(r"\b" + re.escape(signature) + r"\s*\([^;]*?\)\s*\{", text)
    if match is None:
        raise AssertionError("Source function missing: " + signature)
    start = text.index("{", match.start())
    depth = 0
    state = "code"
    i = start
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state == "code":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and nxt == "*":
                state = "comment"
                i += 1
            elif char == "/" and nxt == "/":
                state = "line-comment"
                i += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return text[match.start():i + 1]
        elif state == "string":
            if char == "\\":
                i += 1
            elif char == '"':
                state = "code"
        elif state == "char":
            if char == "\\":
                i += 1
            elif char == "'":
                state = "code"
        elif state == "comment" and char == "*" and nxt == "/":
            state = "code"
            i += 1
        elif state == "line-comment" and char == "\n":
            state = "code"
        i += 1
    raise AssertionError("Unclosed function body: " + signature)


class TExpStageScratchTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        melee = ROOT / ".deps/melee"
        emcc = ROOT / ".deps/emsdk/upstream/emscripten/emcc"
        prepared = ROOT / "build/gameplay-source-slippi-profile/src"
        if not melee.is_dir() or not emcc.is_file():
            raise unittest.SkipTest("pinned source checkout/Emscripten compiler unavailable")
        if not all((prepared / name).is_file() for name in (
            "sysdolphin/baselib/texp.h", "sysdolphin/baselib/tobj.h")):
            raise unittest.SkipTest("existing prepared SDK headers unavailable; no runtime build in this control")
        external = os.environ.get("MELEE_TEXP_STAGE_EVIDENCE")
        if external:
            parent = Path(external)
            if parent.is_symlink():
                raise ValueError("external evidence directory cannot be a symlink")
            parent.mkdir(parents=True, exist_ok=True)
            cls.work = Path(tempfile.mkdtemp(prefix="texp-stage-scratch-", dir=parent))
            cls._external_evidence = True
        else:
            cls.work = cls.new_workspace(ROOT, "texp-stage-scratch-")
            cls._external_evidence = False

    def command(self, label, argv, cwd, env, expected=0, timeout=120):
        argv = list(map(str, argv))
        result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                                text=True, timeout=timeout)
        (self.work / (label + ".json")).write_text(json.dumps({
            "argv": argv, "cwd": str(cwd), "exit_code": result.returncode,
            "stdout": result.stdout, "stderr": result.stderr,
        }, indent=2) + "\n")
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_original_vs_private_setup_preserves_source_and_gx_order(self):
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        melee = ROOT / ".deps/melee"
        commit = lock["repositories"]["melee"]["commit"]
        relative = "src/sysdolphin/baselib/texp.c"
        tev_relative = "src/sysdolphin/baselib/tev.c"
        original_texp = subprocess.check_output(
            ["git", "show", commit + ":" + relative], cwd=melee, text=True)
        original_tev = subprocess.check_output(
            ["git", "show", commit + ":" + tev_relative], cwd=melee, text=True)

        # Compose and apply the same transformed base plus private overlay used
        # by source preparation. Only this exact source file is materialized.
        base_patch = composed_patch(melee, ROOT / "patches/melee-gameplay.patch")
        combined_patch = compose_slippi_profile_patch(
            melee, base_patch, ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch")
        source_tree = self.work / "sequential-source"
        source_file = source_tree / relative
        source_file.parent.mkdir(parents=True)
        source_file.write_text(original_texp)
        env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(self.work))
        for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
            env.pop(key, None)
        self.command("base-check", ["git", "apply", "--check", "--include=" + relative,
                                    base_patch], source_tree, env)
        self.command("base-apply", ["git", "apply", "--include=" + relative,
                                    base_patch], source_tree, env)
        self.command("profile-check", ["git", "apply", "--check", "--include=" + relative,
                                       ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"],
                     source_tree, env)
        self.command("profile-apply", ["git", "apply", "--include=" + relative,
                                       ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"],
                     source_tree, env)
        combined_root = source_tree / "combined"
        composed = combined_root / relative
        composed.parent.mkdir(parents=True)
        composed.write_text(original_texp)
        self.command("combined-check", ["git", "apply", "--check", "--include=" + relative,
                                         combined_patch], combined_root, env)
        self.command("combined-apply", ["git", "apply", "--include=" + relative,
                                         combined_patch], combined_root, env)
        candidate_text = source_file.read_text()
        self.assertEqual(composed.read_text(), candidate_text,
                         "sequential and canonical patch composition must match")

        original_setup = function(original_texp, "HSD_TExpSetupTev").replace(
            "HSD_TExpSetupTev(", "reference_HSD_TExpSetupTev(", 1)
        candidate_setup = function(candidate_text, "HSD_TExpSetupTev").replace(
            "HSD_TExpSetupTev(", "candidate_HSD_TExpSetupTev(", 1)
        actual_stage = function(original_tev, "HSD_SetupTevStage")
        template = (ROOT / "src/slippi_profile/tests/texp_stage_scratch_control.c.in").read_text()
        fixture = template.replace("ORIGINAL_SETUP", "void " + original_setup)
        fixture = fixture.replace("CANDIDATE_SETUP", "void " + candidate_setup)
        fixture = fixture.replace("ACTUAL_SETUP_STAGE", "void " + actual_stage)
        self.assertNotIn("ORIGINAL_SETUP", fixture)
        self.assertNotIn("CANDIDATE_SETUP", fixture)
        self.assertNotIn("ACTUAL_SETUP_STAGE", fixture)
        control = self.work / "control.c"
        control.write_text(fixture)

        prepared = ROOT / "build/gameplay-source-slippi-profile/src"
        emcc = ROOT / ".deps/emsdk/upstream/emscripten/emcc"
        self.assertTrue(emcc.is_file(), "pinned Emscripten compiler unavailable")
        self.assertTrue((prepared / "sysdolphin/baselib/texp.h").is_file(),
                        "prepared source headers unavailable")
        common = [
            "-std=gnu11", "-O1", "-ffp-contract=off", "-DTARGET_PC", "-DAURORA",
            "-I" + str(ROOT / "src"), "-I" + str(prepared),
            "-I" + str(ROOT / ".deps/aurora/include"),
            "-I" + str(ROOT / ".deps/melee/extern/dolphin/include"),
            "-include", str(ROOT / "src/gameplay_compat.h"), str(control),
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sWASM_ASYNC_COMPILATION=0",
        ]
        runs = []
        for label, enabled in (("private", True), ("vanilla", False)):
            output = self.work / (label + ".js")
            argv = [emcc, *common]
            argv.insert(4, "-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=" + ("1" if enabled else "0"))
            argv.extend(["-o", str(output)])
            self.command("compile-" + label, argv, ROOT, env)
            execution = self.command("run-" + label,
                                     [node_runtime(ROOT), output], ROOT, env)
            self.assertIn("TExp stage source control passed", execution.stdout)
            runs.append({"lane": label, "stdout": execution.stdout.strip(),
                         "js_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
                         "wasm_sha256": hashlib.sha256(output.with_suffix(".wasm").read_bytes()).hexdigest()})

        pins = [
            ROOT / "dependencies.lock.json", ROOT / "patches/melee-gameplay.patch",
            ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch",
            ROOT / "src/slippi_profile/texp_stage_scratch.h",
            ROOT / "src/slippi_profile/tests/texp_stage_scratch_control.c.in",
            Path(__file__), melee / "src/sysdolphin/baselib/texp.c",
            melee / "src/sysdolphin/baselib/tev.c",
            prepared / "sysdolphin/baselib/texp.h",
            prepared / "sysdolphin/baselib/tobj.h",
        ]
        report = {
            "scope": "actual source HSD_TExpSetupTev and HSD_SetupTevStage; modeled State/GX sinks",
            "source_commit": commit,
            "source_sha256": {
                str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in pins
            },
            "base_composed_patch_sha256": hashlib.sha256(base_patch.read_bytes()).hexdigest(),
            "full_profile_composed_patch_sha256": hashlib.sha256(combined_patch.read_bytes()).hexdigest(),
            "sequential_tExp_sha256": hashlib.sha256(candidate_text.encode()).hexdigest(),
            "control_sha256": hashlib.sha256(control.read_bytes()).hexdigest(),
            "runs": runs,
            "limits": ["modeled GX sink; no pixel or runtime ownership claim",
                       "this control does not attribute prior strict mismatches to TEv descriptors"],
        }
        (self.work / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print("Retained TExp stage source control:", self.work, flush=True)


if __name__ == "__main__":
    unittest.main()
