"""Compile the native menu lifecycle contract with source GObj ordering."""
from pathlib import Path
import os
import subprocess
import sys
import unittest
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime



class GameplayMenuContractTests(OwnedWorkspaceTests):
    def test_lifecycle_boundary_and_return_contract(self):
        self.run_contract(public=False)

    def test_public_donkey_selection_at_css_and_final_match_handoff(self):
        self.run_contract(public=True)

    def test_stadium_diagnostic_explicit_confirm_owner_lifetime(self):
        self.run_contract(public=False, stadium=True)
        # The reduced trace checks content-only availability even while armed.
        # Bind that rejection to the actual prepared random-stage function,
        # which must never call the explicit-tile diagnostic permission.
        source = (ROOT / "build/gameplay-source/src/melee/mn/mnstagesel.c").read_text()
        random_selection = source.split("int mnStageSel_802599EC(void)", 1)[1].split(
            "void mnStageSel_80259C28(void)", 1)[0]
        self.assertIn("MELEE_WEB_MENU_STAGE_AVAILABLE", random_selection)
        self.assertNotIn("MELEE_WEB_MENU_STAGE_EXPLICIT_CONFIRM_AVAILABLE", random_selection)

    def run_contract(self, *, public, stadium=False):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/emcc"
        config = sdk / ".emscripten"
        if not compiler.is_file() or not config.is_file() or not (
            ROOT / "build/gameplay-source/src/melee/mn/types.h"
        ).is_file():
            self.skipTest("Prepared Emscripten SDK unavailable; build gameplay first")
        cache = compiler.parent / "cache"
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(cache), EMSDK_PYTHON=sys.executable)
        directory = self.new_workspace(ROOT, "melee-gameplay-menu-")
        output = Path(directory) / "gameplay_menu_contract.js"
        result = subprocess.run(
            [
                str(compiler), "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-variable", "-DAURORA",
                *(["-DMELEE_WEB_PUBLIC_RUNTIME"] if public else []),
                *(["-DMELEE_WEB_STADIUM_C1A_DIAGNOSTIC"] if stadium else []),
                "-DTARGET_PC", "-I", str(ROOT / "src"), "-I",
                str(ROOT / "build/gameplay-source/src"), "-I",
                str(ROOT / ".deps/aurora/include"), "-I",
                str(ROOT / ".deps/melee/extern/dolphin/include"), "-O1",
                "-std=gnu11", "-fexceptions", "-ffunction-sections",
                "-fdata-sections", "-ffp-contract=off", "-include",
                str(ROOT / "src/gameplay_compat.h"),
                str(ROOT / "src/gameplay_menu.c"),
                str(ROOT / "src/gameplay_match_rules.c"),
                str(ROOT / ".deps/melee/src/sysdolphin/baselib/gobjplink.c"),
                str(ROOT / "tests/native_menu_fighter_input.c"),
                str(ROOT / "tests/gameplay_menu_trace.c"),
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(output),
            ], cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        run = subprocess.run(
            [str(node_runtime()), str(output)], cwd=ROOT, env=env,
            capture_output=True, text=True, timeout=20,
        )
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("sparse-port admission: dense count=2 CSS=1 SSS=1; "
                      "P1+P3 count=2 CSS=1 SSS=1; source ports 0/2 retained", run.stdout)
        self.assertIn("native menu lifecycle contract trace: passed", run.stdout)


if __name__ == "__main__":
    unittest.main()
