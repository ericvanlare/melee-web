# SPDX-License-Identifier: GPL-3.0-or-later
"""Pointer-free source match identity and live selection accessor controls."""
from pathlib import Path
import os
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


class RuntimeMatchIdentityControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.emxx = ROOT / ".deps/emsdk/upstream/emscripten/em++"
        if not cls.emxx.is_file():
            raise unittest.SkipTest("Pinned Emscripten C++ compiler unavailable")
        cls.work = cls.new_workspace(ROOT, "runtime-match-identity-")
        # Compose the exact checked profile overlay from the pinned source and
        # reviewed patch. Do not assume a private diagnostic build happened to
        # leave its generated source tree behind on this machine.
        cls.profile_source = prepare_sources(ROOT, slippi_profile=True)

    def common(self):
        return ["-std=c++20", "-O1", "-UNDEBUG", "-DTARGET_PC",
            "-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1",
            "-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1",
            "-I" + str(ROOT / "src"),
            "-I" + str(self.profile_source),
            "-I" + str(self.profile_source / "melee/gm"),
            "-I" + str(self.profile_source / "melee/lb"),
            "-I" + str(ROOT / ".deps/melee/src"),
            "-I" + str(ROOT / ".deps/aurora/include"),
            "-I" + str(ROOT / ".deps/melee/extern/dolphin/include"),
            "-include", str(ROOT / "src/gameplay_compat.h")]

    def test_actual_match_session_selection_getter_translation_unit(self):
        result = subprocess.run([str(self.emxx), *self.common(), "-fsyntax-only",
            str(ROOT / "src/gameplay_match_session.cpp")], cwd=ROOT,
            capture_output=True, text=True, timeout=120)
        (self.work / "session-syntax.log").write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_source_backed_identity_serializer_controls(self):
        directory = self.work / "identity"
        directory.mkdir()
        tmp = directory / "tmp"
        tmp.mkdir()
        binary = directory / "control.js"
        env = dict(os.environ, TMPDIR=str(tmp))
        compiled = subprocess.run([str(self.emxx), *self.common(),
            "-fexceptions", "-sDISABLE_EXCEPTION_CATCHING=0",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            str(ROOT / "src/slippi_profile/tests/runtime_match_identity_control.cpp"),
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-o", str(binary)],
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
        (directory / "compile.log").write_text(compiled.stdout + compiled.stderr)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        run = subprocess.run([str(node_runtime(ROOT)), str(binary)], cwd=ROOT,
            env=env, capture_output=True, text=True, timeout=30)
        (directory / "run.log").write_text(run.stdout + run.stderr)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("Source-backed pointer-free match identity controls passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
