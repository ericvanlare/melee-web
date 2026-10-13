"""Focused contracts for the private, opt-in gameplay FMA diagnostic."""

import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from owned_test_workspace import OwnedWorkspaceTests

try:
    import resource
except ImportError:
    resource = None


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("gameplay_fma_build", ROOT / "scripts/build.py")
BUILD = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(BUILD)


def _no_core_dump():
    if resource is not None:
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


class GameplayFmaDiagnosticTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "gameplay-fma-diagnostic-")

    def test_build_profile_is_private_explicit_release_and_isolated(self):
        root = Path("/fixture/repo")
        target = BUILD.FMA_DIAGNOSTIC_TARGET
        self.assertEqual(BUILD.BUILD_TARGETS[target], ("gameplay_menu_browser",))
        collision = BUILD.build_directory(
            root, target=target, configuration="Release",
            fma_collision_diagnostic=True)
        velocity = BUILD.build_directory(
            root, target=target, configuration="Release",
            fma_velocity_y_diagnostic=True)
        combined = BUILD.build_directory(
            root, target=target, configuration="Release",
            fma_collision_diagnostic=True, fma_velocity_y_diagnostic=True)
        self.assertEqual(collision, root / "build/browser-fma-collision-release")
        self.assertEqual(velocity, root / "build/browser-fma-velocity-y-release")
        self.assertEqual(combined, root / "build/browser-fma-collision-velocity-y-release")
        self.assertEqual(len({collision, velocity, combined}), 3)

        with self.assertRaisesRegex(ValueError, "requires at least one explicit"):
            BUILD.build_directory(root, target=target, configuration="Release")
        with self.assertRaisesRegex(ValueError, "require --target"):
            BUILD.build_directory(root, target="runtime", configuration="Release",
                                  fma_collision_diagnostic=True)
        for kwargs, message in (
            ({"configuration": "RelWithDebInfo"}, "Release-only"),
            ({"configure_only": True}, "cannot be used"),
            ({"pipeline_provenance": True}, "cannot be combined"),
            ({"selective_pipelines": True}, "cannot be combined"),
        ):
            with self.subTest(kwargs=kwargs), patch.object(BUILD, "read_lock") as read_lock:
                build_kwargs = {"configuration": "Release",
                                "fma_collision_diagnostic": True}
                build_kwargs.update(kwargs)
                with self.assertRaisesRegex(ValueError, message):
                    BUILD.build(1, root=Path("/missing/repo"), target=target,
                                **build_kwargs)
                read_lock.assert_not_called()

    def test_inventory_matches_helper_and_every_mapped_site_is_used_once(self):
        helper = (ROOT / "src/gameplay_fma_diagnostic.c").read_text(encoding="utf-8")
        header = (ROOT / "src/gameplay_fma_diagnostic.h").read_text(encoding="utf-8")
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        fighter_cmake = (ROOT / "cmake/FighterRuntime.cmake").read_text(encoding="utf-8")
        gameplay_target = cmake.split("add_library(gameplay_runtime STATIC", 1)[1].split(
            "# Native class construction", 1)[0]
        patch_text = (ROOT / "patches/melee-gameplay.patch").read_text(encoding="utf-8")
        table = re.search(
            r"collision_sites\[MELEE_WEB_FMA_COLLISION_SITE_COUNT\] = \{(.*?)\n\};",
            helper, re.S)
        self.assertIsNotNone(table)
        helper_sites = tuple(re.findall(r'\{"([0-9a-f]{8})", "([^"]+)"\}', table.group(1)))
        self.assertEqual(helper_sites, BUILD.FMA_COLLISION_OPERATION_INVENTORY)
        self.assertEqual(len(helper_sites), 37)

        enum = header.split("enum MeleeWebFmaCollisionSite {", 1)[1].split("};", 1)[0]
        enum_sites = re.findall(r"MELEE_WEB_FMA_([A-Z0-9_]+)", enum)
        self.assertEqual(len(enum_sites), 38)  # 37 operations and the count sentinel.
        section_start = patch_text.index(
            "diff --git a/src/melee/lb/lbcollision.c b/src/melee/lb/lbcollision.c")
        section_end = patch_text.find("\ndiff --git ", section_start + 1)
        collision_patch = patch_text[section_start:] if section_end < 0 else \
            patch_text[section_start:section_end]
        added_code = "\n".join(
            line[1:] for line in collision_patch.splitlines()
            if line.startswith("+") and not line.startswith("+++"))
        uses = re.findall(
            r"MELEE_WEB_FMA_COLLISION_(?:ADD|SUB)\(\s*(MELEE_WEB_FMA_[A-Z0-9_]+)",
            added_code)
        self.assertEqual(len(uses), 37)
        self.assertEqual(len(set(uses)), 37)
        self.assertEqual(set(name for name in enum_sites if name != "COLLISION_SITE_COUNT"),
                         set(site.removeprefix("MELEE_WEB_FMA_") for site in uses))
        self.assertEqual(BUILD.FMA_VELOCITY_Y_OPERATION,
                         ("8006bc90", "common_velocity_y_interpolation"))
        self.assertIn("MELEE_WEB_FMA_COLLISION_DIAGNOSTIC\n  \"Enable the private PC-mapped scalar collision FMA diagnostic\" OFF", cmake)
        self.assertIn("MELEE_WEB_FMA_VELOCITY_Y_DIAGNOSTIC\n  \"Enable the private common fighter velocity-Y interpolation FMA diagnostic\" OFF", cmake)
        self.assertIn("FMA diagnostics are private and unavailable in public runtimes", cmake)
        self.assertIn("FMA diagnostics require the Emscripten gameplay runtime", cmake)
        self.assertIn('"${MELEE_WEB_GAMEPLAY_SOURCE_DIR}/melee/ft/fighter.c"', gameplay_target)
        self.assertIn("target_sources(gameplay_runtime PRIVATE src/gameplay_fma_diagnostic.c)",
                      gameplay_target)
        self.assertIn("MELEE_WEB_FMA_VELOCITY_Y_DIAGNOSTIC=1", gameplay_target)
        self.assertNotIn("target_sources(fighter_source_runtime PRIVATE src/gameplay_fma_diagnostic.c)",
                         fighter_cmake)
        self.assertIn("target_compile_definitions(fighter_source_runtime PRIVATE\n      MELEE_WEB_FMA_COLLISION_DIAGNOSTIC=1)",
                      fighter_cmake)

    def test_default_off_and_enabled_native_arithmetic_reporting_and_domain(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required for the native arithmetic control")

        harness = ROOT / "tests/gameplay_fma_diagnostic_control.c"
        helper = ROOT / "src/gameplay_fma_diagnostic.c"
        off_binary = self.workspace / "default-off-control"
        enabled_binary = self.workspace / "enabled-control"
        common = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-ffp-contract=off", "-I" + str(ROOT / "src")]
        compile_off = subprocess.run(
            [*common, str(harness), "-lm", "-o", str(off_binary)],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(compile_off.returncode, 0, compile_off.stdout + compile_off.stderr)
        off = subprocess.run([str(off_binary), "bits"], capture_output=True,
                             text=True, timeout=10)
        self.assertEqual(off.returncode, 0, off.stdout + off.stderr)
        off_bits = dict(re.findall(r"(\w+)=([0-9a-f]{8})", off.stdout))
        self.assertEqual(off_bits, {
            "candidate": "00000000", "split": "00000000",
            "tie": "bf55bf18", "negative_zero": "80000000",
        })

        enabled = ["-DMELEE_WEB_FMA_DIAGNOSTIC=1",
                   "-DMELEE_WEB_FMA_COLLISION_DIAGNOSTIC=1",
                   "-DMELEE_WEB_FMA_VELOCITY_Y_DIAGNOSTIC=1"]
        compile_enabled = subprocess.run(
            [*common, *enabled, str(helper), str(harness), "-lm", "-o", str(enabled_binary)],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(compile_enabled.returncode, 0,
                         compile_enabled.stdout + compile_enabled.stderr)
        bits = subprocess.run([str(enabled_binary), "bits"], capture_output=True,
                              text=True, timeout=10)
        self.assertEqual(bits.returncode, 0, bits.stdout + bits.stderr)
        enabled_bits = dict(re.findall(r"(\w+)=([0-9a-f]{8})", bits.stdout))
        self.assertEqual(enabled_bits, {
            "candidate": "a8800000", "split": "00000000",
            "tie": "bf55bf17", "negative_zero": "80000000",
        })

        for mode, expected_executed in (("summary", False), ("summary-y", True)):
            with self.subTest(mode=mode):
                result = subprocess.run([str(enabled_binary), mode], capture_output=True,
                                        text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                report = json.loads(result.stdout)
                diagnostic = report["fma_diagnostic"]
                self.assertEqual(diagnostic["cursor"], 5239)
                self.assertEqual(diagnostic["collision"]["site_count"], 37)
                self.assertEqual(len(diagnostic["collision"]["sites"]), 37)
                first = diagnostic["collision"]["sites"][0]["observation"]
                self.assertEqual(first, {
                    "calls": 1,
                    "last_operands": ["3f800000", "40000000", "40400000"],
                    "last_result": "40a00000",
                })
                velocity = diagnostic["velocity_y"]
                self.assertEqual(velocity["candidate_executed"], expected_executed)
                self.assertEqual(velocity["observation"] is not None, expected_executed)

        for mode in ("reject-nan", "reject-infinity", "reject-subnormal",
                     "reject-ni-underflow", "reject-ni-boundary-positive",
                     "reject-ni-boundary-negative"):
            with self.subTest(mode=mode):
                rejected = subprocess.run([str(enabled_binary), mode], capture_output=True,
                                          text=True, timeout=10,
                                          preexec_fn=_no_core_dump if os.name == "posix" else None)
                self.assertNotEqual(rejected.returncode, 0)
                if "boundary" in mode:
                    self.assertIn("ambiguous NI boundary", rejected.stderr)
                else:
                    self.assertIn("FMA_DIAGNOSTIC unsupported", rejected.stderr)


if __name__ == "__main__":
    unittest.main()
