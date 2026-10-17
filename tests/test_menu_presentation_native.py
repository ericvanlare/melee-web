"""Actual presenter extraction with synthetic endpoint/order controls; no GPU proof."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests
ROOT = Path(__file__).resolve().parents[1]
BODY_SHA = "6f9fb16173e8c861a093a2cc2956a49d3df05f93bc4b83a07304c2dd263a63e4"
class MenuPresentationNativeTests(OwnedWorkspaceTests):
    def test_actual_shared_presenter_order_and_references(self):
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("clang++ required for actual presenter endpoint controls")
        source = (ROOT / "src/gameplay_menu_browser.cpp").read_text()
        start = source.index("// Synchronous borrowed callback state only;")
        closing = " // PRESENTATION_BODY_END\n}"
        end = source.index(closing, start) + len(closing)
        implementation = source[start:end]
        body_start = implementation.index(" // PRESENTATION_BODY_BEGIN\n") + len(" // PRESENTATION_BODY_BEGIN\n")
        body_end = implementation.index("\n // PRESENTATION_BODY_END", body_start)
        self.assertEqual(hashlib.sha256(implementation[body_start:body_end].encode()).hexdigest(), BODY_SHA,
                         "Reviewed original presenter body changed; inspect order/branch baseline before updating")
        self.assertNotIn("present_source_impl", source)
        self.assertEqual(source.count("present_source_frame(presentation_context"), 1)
        fixture = (ROOT / "tests/menu_presentation_native_controls.cpp").read_text()
        self.assertEqual(fixture.count("// ACTUAL_PRESENTATION_IMPLEMENTATION"), 1)
        scratch = self.new_workspace(ROOT, "menu-presentation-native-")
        cpp = scratch / "actual-presentation.cpp"
        cpp.write_text(fixture.replace("// ACTUAL_PRESENTATION_IMPLEMENTATION", implementation))
        for name, flags in (("diagnostic", ["-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC", "-DMELEE_WEB_PIPELINE_PROVENANCE"]),
                            ("public", ["-DMELEE_WEB_PUBLIC_RUNTIME"])):
            output = scratch / name
            argv = [compiler, "-std=c++20", "-O3", "-DNDEBUG", *flags,
                    "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
                    str(cpp), "-o", str(output)]
            build = subprocess.run(argv, capture_output=True, text=True, timeout=90)
            (scratch / (name + "-compile.log")).write_text(build.stdout + build.stderr)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(output)], capture_output=True, text=True, timeout=30)
            (scratch / (name + "-run.log")).write_text(run.stdout + run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("actual shared presentation 11 endpoint-order/refusal cases passed", run.stdout)
