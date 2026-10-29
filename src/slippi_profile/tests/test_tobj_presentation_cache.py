# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused actual-source controls for the private TObj matrix scratch leaf."""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(os.environ.get(
    "MELEE_TOBJ_CACHE_REPO_ROOT", Path(__file__).resolve().parents[3]
)).resolve()
SOURCE = Path(os.environ.get("MELEE_TOBJ_CACHE_CANDIDATE_ROOT", ROOT)).resolve()
_saved_path = sys.path.copy()
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "scripts")]
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
from gameplay_bool import composed_patch, transform_source
from gameplay_sources import compose_slippi_profile_patch
sys.path[:] = _saved_path


def function(text, name):
    match = re.search(
        r"^(?:static )?(?:void|HSD_TObj\*) " + re.escape(name) +
        r"\([^;]*?\)\s*\{", text, re.M,
    )
    if not match:
        raise AssertionError("Actual function missing: " + name)
    i = text.index("{", match.start()) + 1
    depth = 1
    while depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    return text[match.start():i]


def selected(relative):
    candidate = SOURCE / relative
    return candidate if candidate.is_file() else ROOT / relative


class TObjPresentationCacheTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        external = os.environ.get("MELEE_TOBJ_CACHE_OUTPUT_ROOT")
        if not external:
            cls.work = cls.new_workspace(ROOT, "tobj-scratch-")
            return
        requested = Path(external)
        if requested.is_symlink():
            raise ValueError("external evidence symlink refused")
        parent = requested.resolve()
        parent.mkdir(parents=True, exist_ok=True)
        cls.work = Path(tempfile.mkdtemp(prefix="tobj-scratch-", dir=parent))
        cls._workspace_failed = False
        cls._workspace_tests_started = False
        cls._workspace_result = None

    def run_command(self, name, argv, cwd=None, env=None, expected=0):
        command = list(map(str, argv))
        record = {"argv": command, "cwd": str(cwd or self.work)}
        try:
            proc = subprocess.run(
                command, cwd=cwd or self.work, env=env, text=True,
                capture_output=True, timeout=180,
            )
            record.update(returncode=proc.returncode,
                          stdout=proc.stdout, stderr=proc.stderr)
        except subprocess.TimeoutExpired as exc:
            record.update(
                returncode=None, timed_out=True,
                stdout=(exc.stdout or b"").decode(errors="replace")
                if isinstance(exc.stdout, bytes) else exc.stdout,
                stderr=(exc.stderr or b"").decode(errors="replace")
                if isinstance(exc.stderr, bytes) else exc.stderr,
            )
            (self.work / (name + ".json")).write_text(
                json.dumps(record, indent=2) + "\n"
            )
            raise
        (self.work / (name + ".json")).write_text(
            json.dumps(record, indent=2) + "\n"
        )
        self.assertEqual(proc.returncode, expected, proc.stdout + proc.stderr)
        return proc.stdout, proc.stderr

    def test_actual_source_matrix_scratch_matches_original(self):
        emscripten = ROOT / ".deps/emsdk/upstream/emscripten"
        if not (emscripten / "emcc").is_file():
            self.skipTest("Pinned Emscripten unavailable")
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        self.assertEqual(
            (emscripten / "emscripten-version.txt").read_text().strip().strip('"'),
            lock["emscripten"],
        )
        compile_db = Path(os.environ.get(
            "MELEE_TOBJ_CACHE_COMPILE_COMMANDS",
            ROOT / "build/slippi-synctest-logical-frame-lobj-presentation-stage-"
                 "arena-aurora-staging-release/compile_commands.json",
        ))
        if not compile_db.is_file():
            if "MELEE_TOBJ_CACHE_COMPILE_COMMANDS" in os.environ:
                self.fail("Explicit compile graph missing")
            self.skipTest("Existing private compile graph unavailable")
        entries = json.loads(compile_db.read_text())
        class_rows = [row for row in entries
                      if row["file"].endswith("/src/sysdolphin/baselib/class.c")]
        self.assertEqual(len(class_rows), 1)
        prepared = Path(class_rows[0]["file"]).resolve().parents[2]
        for relative in (
            "Runtime/platform.h", "sysdolphin/baselib/class.h",
            "sysdolphin/baselib/object.h", "sysdolphin/baselib/tobj.h",
        ):
            self.assertTrue((prepared / relative).is_file(), relative)

        profile_patch = selected(
            "src/slippi_profile/patches/melee-profile-hooks.patch"
        )
        cache_source = selected("src/slippi_profile/tobj_presentation_cache.c")
        cache_header = selected("src/slippi_profile/tobj_presentation_cache.h")
        fixture_source = selected(
            "src/slippi_profile/tests/tobj_presentation_owner_control.c.in"
        )
        original_tobj = subprocess.check_output(
            ["git", "show", lock["repositories"]["melee"]["commit"] +
             ":src/sysdolphin/baselib/tobj.c"],
            cwd=ROOT / ".deps/melee", text=True,
        )
        original_setup = function(original_tobj, "TObjSetupMtx")
        self.assertIn("if (tobj->flags & TEX_MTX_DIRTY)", original_setup)
        self.assertIn("HSD_TOBJ_METHOD(tobj)->make_mtx(tobj)", original_setup)
        self.assertIn("switch (tobj_coord(tobj))", original_setup)
        # The checked-in profile patch is the implementation under test; its
        # only setup leaf must gate before the original Toon return and retain
        # the original dirty conditional/body.
        source_tree = self.work / "source"
        source_tree.mkdir()
        env = dict(os.environ, TMPDIR=str(self.work),
                   GIT_CEILING_DIRECTORIES=str(source_tree.parent))
        for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
            env.pop(key, None)
        ceiling = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], cwd=source_tree,
            env=env, text=True, capture_output=True,
        )
        self.assertNotEqual(ceiling.returncode, 0)
        self.run_command(
            "clone", ["git", "clone", "--shared", "--no-checkout",
                      ROOT / ".deps/melee", source_tree], env=env,
        )
        commit = lock["repositories"]["melee"]["commit"]
        self.assertEqual(
            subprocess.check_output(["git", "rev-parse", "HEAD"],
                                    cwd=source_tree, text=True).strip(), commit,
        )
        composed = compose_slippi_profile_patch(
            source_tree,
            composed_patch(source_tree, selected("patches/melee-gameplay.patch")),
            profile_patch,
        )
        relatives = ["src/sysdolphin/baselib/" + name + ".c"
                     for name in ("class", "object", "tobj", "shadow")]
        for relative in relatives:
            path = source_tree / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(subprocess.check_output(
                ["git", "show", commit + ":" + relative], cwd=source_tree,
            ))
        self.run_command(
            "apply-composed",
            ["git", "apply", *["--include=" + item for item in relatives], composed],
            cwd=source_tree, env=env,
        )
        for relative in relatives:
            path = source_tree / relative
            path.write_text(transform_source(Path(relative), path.read_text()))

        patched_tobj = source_tree / relatives[2]
        setup = function(patched_tobj.read_text(), "TObjSetupMtx")
        self.assertLess(setup.index("slippi_tobj_managed_require"),
                        setup.index("if (tobj_coord(tobj) == TEX_COORD_TOON)"))
        self.assertIn("if (tobj->flags & TEX_MTX_DIRTY)", setup)
        self.assertIn("HSD_TOBJ_METHOD(tobj)->make_mtx(tobj)", setup)
        self.assertIn("memcpy(&presentation_scratch, tobj, sizeof(presentation_scratch))",
                      setup)
        cache_text = cache_source.read_text()
        self.assertNotRegex(cache_text, r"\b(?:malloc|calloc|realloc)\s*\(")
        self.assertNotIn("TObjOwner", cache_text)
        self.assertNotIn("slippi_tobj_matrix_require", cache_header.read_text())

        overlay = self.work / "overlay"
        (overlay / "src/slippi_profile/tests").mkdir(parents=True)
        for base in (ROOT, SOURCE):
            for path in (base / "src").rglob("*"):
                if path.is_file() and path.suffix in (".h", ".hpp", ".inc"):
                    dest = overlay / "src" / path.relative_to(base / "src")
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, dest)
        shutil.copy2(cache_source, overlay / "src/slippi_profile/tobj_presentation_cache.c")
        control = overlay / "src/slippi_profile/tests/control.c"
        control.write_text(fixture_source.read_text())

        patched_tobj.write_text(patched_tobj.read_text() + r"""
void fixture_tobj_update_type(HSD_TObj* p,u32 type,float v){HSD_ObjData d;memset(&d,0,sizeof(d));d.fv=v;TObjUpdateFunc(p,(enum_t)type,&d);}
void fixture_tobj_update(HSD_TObj* p,float v){fixture_tobj_update_type(p,HSD_A_T_TRAV,v);}
void fixture_tobj_setup(HSD_TObj* p){TObjSetupMtx(p);}
void fixture_tobj_amnesia(void){TObjAmnesia((HSD_ClassInfo*)&hsdTObj);}
int fixture_tobj_default_callbacks(void){return hsdTObj.make_mtx==MakeTextureMtx&&hsdTObj.load==TObjLoad&&hsdTObj.make_texp==TObjMakeTExp&&hsdTObj.parent.release==TObjRelease&&hsdTObj.parent.amnesia==TObjAmnesia;}
int fixture_reload(HSD_TObj* p,HSD_TObjDesc* d){return HSD_TOBJ_METHOD(p)->load(p,d);}
""")
        shadow = self.work / "shadow-construction.c"
        shadow.write_text(
            '#include <sysdolphin/baselib/tobj.h>\n' +
            function((source_tree / relatives[3]).read_text(), "makeShadowTObj") +
            "\nHSD_TObj* fixture_actual_shadow(void){return makeShadowTObj();}\n"
        )
        math_source = self.work / "rotation.c"
        math_source.write_text(
            '#include <sysdolphin/baselib/mtx.h>\n#include <math.h>\n' +
            function((prepared / "sysdolphin/baselib/mtx.c").read_text(),
                     "HSD_MkRotationMtx") + "\n"
        )

        def compile_one(name, source, lane, suffix):
            rows = [row for row in entries if row["file"].endswith(suffix)]
            self.assertEqual(len(rows), 1, suffix)
            args = shlex.split(rows[0]["command"])
            out_index = args.index("-o")
            del args[out_index:out_index + 2]
            compile_index = args.index("-c")
            del args[compile_index:compile_index + 2]
            args = [arg.replace(str(ROOT / "src/gameplay_compat.h"),
                                str(overlay / "src/gameplay_compat.h"))
                    for arg in args if not arg.startswith((
                        "-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE",
                        "-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE",
                    ))]
            active = lane == "active"
            output = self.work / (lane + "-" + name + ".o")
            args += [
                "-I" + str(overlay / "src"), "-I" + str(prepared),
                "-I" + str(prepared / "sysdolphin/baselib"), "-UNDEBUG",
                "-DPROPOSED_LANE=" + str(int(active)),
                "-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=" + str(int(active)),
                "-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=" + str(int(active)),
                "-c", str(source), "-o", str(output),
            ]
            self.run_command(lane + "-compile-" + name, args, env=env)
            return output

        source_map = [
            ("class", source_tree / relatives[0], "/src/sysdolphin/baselib/class.c"),
            ("object", source_tree / relatives[1], "/src/sysdolphin/baselib/class.c"),
            ("tobj", patched_tobj, "/src/sysdolphin/baselib/tobj.c"),
            ("shadow", shadow, "/src/sysdolphin/baselib/tobj.c"),
            ("rotation", math_source, "/src/sysdolphin/baselib/tobj.c"),
            ("psmath", ROOT / "src/gameplay_ps_math.c", "/src/gameplay_ps_math.c"),
            ("sdkmtx", ROOT / ".deps/aurora/lib/dolphin/mtx/mtx.c",
             "/lib/dolphin/mtx/mtx.c"),
            ("bounds", ROOT / "src/hsd_texture_bounds.c", "/src/hsd_texture_bounds.c"),
            ("control", control, "/src/sysdolphin/baselib/tobj.c"),
        ]
        outputs = {}
        for lane in ("original", "active"):
            objects = [compile_one(name, path, lane, suffix)
                       for name, path, suffix in source_map]
            binary = self.work / (lane + ".js")
            self.run_command(
                lane + "-link", [emscripten / "emcc", *objects, "-fexceptions",
                                 "-sDISABLE_EXCEPTION_CATCHING=0", "-sENVIRONMENT=node",
                                 "-sEXIT_RUNTIME=1", "-Wl,--wrap=malloc",
                                 "-Wl,--wrap=calloc", "-o", binary], env=env,
            )
            cases = (
                "changed", "unchanged", "same-value", "toon", "uv-clean-matrix",
                "reuse", "shadow-original", "reflection-unchanged",
                "reflection-same-value", "reflection-changed", "reflection-reload",
                "reflection-bump",
            )
            for case in cases:
                stdout, stderr = self.run_command(
                    lane + "-" + case, [node_runtime(), binary, case], env=env,
                )
                outputs[(lane, case)] = (stdout, stderr)
                self.assertRegex(stderr,
                                 r"SOURCE_CLASS_IDENTITY:[^\n]+:0x[0-9a-fA-F]+:HSD_TObj",
                                 case)
            for case in ("active-interval-load",) if lane == "active" else ():
                stdout, stderr = self.run_command(
                    lane + "-" + case, [node_runtime(), binary, case], env=env,
                )
                self.assertIn("ACTUAL:canonical TObj construction, Load and scratch Setup", stdout)
                self.assertRegex(stderr,
                                 r"SOURCE_CLASS_IDENTITY:[^\n]+:0x[0-9a-fA-F]+:HSD_TObj",
                                 case)

        positive_active = (
            "active-authored-write", "active-amnesia", "normal-amnesia",
            "custom-selection", "rebind", "descriptor-change", "world-recreation",
        )
        for case in positive_active:
            stdout, _ = self.run_command(
                "active-" + case, [node_runtime(), self.work / "active.js", case], env=env,
            )
            self.assertIn("ACTUAL:", stdout, case)
        for mode in ("timg-null-table", "repeat-null"):
            for lane in ("original", "active"):
                _, stderr = self.run_command(
                    lane + "-" + mode,
                    [node_runtime(), self.work / (lane + ".js"), mode],
                    env=env, expected=93,
                )
                self.assertIn("SOURCE_ASSERT:", stderr, mode)
                self.assertNotIn("Unsupported Slippi TObj presentation owner", stderr)

        panic_cases = {
            "unsupported-highlight": 9,
            "missing-world-load": 10,
            "toon-custom": 12,
            "toon-descriptor": 2,
            "toon-sticky": 9,
            "toon-missing-world": 10,
        }
        for case, code in panic_cases.items():
            _, stderr = self.run_command(
                "active-" + case,
                [node_runtime(), self.work / "active.js", case],
                env=env, expected=94,
            )
            self.assertRegex(stderr,
                             rf'FIRST_FAILURE_CONTEXT=\{{"code":{code},"line":\d+,"detail":\d+\}}',
                             case)
            self.assertIn("Unsupported Slippi TObj presentation owner", stderr)

        for case in (
            "changed", "unchanged", "same-value", "toon", "uv-clean-matrix",
            "reuse", "shadow-original", "reflection-unchanged",
            "reflection-same-value", "reflection-changed", "reflection-reload",
            "reflection-bump",
        ):
            original = outputs[("original", case)]
            active = outputs[("active", case)]
            # stdout is the complete semantic fixture output (GX arguments and
            # scenario result); lane-specific source diagnostics stay on stderr.
            self.assertEqual(original[0], active[0], case)
            self.assertTrue(any(
                line.startswith(("ACTUAL:", "FIXTURE:"))
                for line in active[0].splitlines()
            ), case)
            if case.startswith("reflection-") or case in ("changed", "unchanged",
                                                             "same-value", "uv-clean-matrix"):
                self.assertEqual(original[0].count("GX:"), active[0].count("GX:"), case)
                self.assertEqual(
                    outputs[("active", case)][1].count("SOURCE172_ACTIVE_DRAW:UNCHANGED"),
                    active[0].count("GX:"), case,
                )
        (self.work / "input-pins.json").write_text(json.dumps({
            "compile_graph": str(compile_db),
            "compile_graph_sha256": hashlib.sha256(compile_db.read_bytes()).hexdigest(),
            "profile_patch": str(profile_patch),
            "profile_patch_sha256": hashlib.sha256(profile_patch.read_bytes()).hexdigest(),
            "cache_source_sha256": hashlib.sha256(cache_source.read_bytes()).hexdigest(),
            "cache_header_sha256": hashlib.sha256(cache_header.read_bytes()).hexdigest(),
            "fixture_sha256": hashlib.sha256(fixture_source.read_bytes()).hexdigest(),
            "composed_patch_sha256": hashlib.sha256(composed.read_bytes()).hexdigest(),
            "source": {relative: hashlib.sha256((source_tree / relative).read_bytes()).hexdigest()
                       for relative in relatives},
            "scope": (
                "actual TObj/class source leaf control; both lanes assert canonical "
                "HSD_TObj class identity and retain relocated raw class pointers "
                "separately; entire stdout semantic result compared; active lane "
                "checks full 172-byte source preservation for scratch matrix draws; "
                "modeled memory/null-AObj, identity camera provider for Shadow, GX sink; "
                "no full TObj resources, texgen, PObj, renderer, or CPU restore claim"
            ),
        }, indent=2) + "\n")
        print("PASS actual TObj matrix scratch/component scope only", self.work)


if __name__ == "__main__":
    unittest.main()
