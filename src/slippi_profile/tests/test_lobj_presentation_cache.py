# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a narrow actual-source constructor, draw, retirement and reuse control."""
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
    "MELEE_LOBJ_CACHE_REPO_ROOT", Path(__file__).resolve().parents[3]
)).resolve()
_saved_path = sys.path.copy()
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "scripts"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
from gameplay_bool import transform_source
sys.path[:] = _saved_path


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def retained_workspace(test_class):
    external = os.environ.get("MELEE_LOBJ_CACHE_OUTPUT_ROOT")
    if not external:
        return test_class.new_workspace(ROOT, "lobj-owner-control-")
    requested_parent = Path(external)
    if requested_parent.is_symlink():
        raise ValueError("external evidence root must not be a symlink")
    parent = requested_parent.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    if parent.is_symlink() or parent.resolve() != parent:
        raise ValueError("external evidence root must be a real directory")
    directory = Path(tempfile.mkdtemp(prefix="lobj-owner-control-", dir=parent))
    inode = directory.stat().st_ino
    test_class._workspace_failed = False
    test_class._workspace_tests_started = False
    test_class._workspace_result = None

    def cleanup():
        keep = (os.environ.get("MELEE_KEEP_TEST_ARTIFACTS") == "1"
                or test_class._workspace_failed)
        if keep:
            print(f"Retained test evidence: {directory}", file=sys.stderr)
            return
        if (directory.is_symlink() or directory.resolve() != directory
                or directory.stat().st_ino != inode):
            raise ValueError("test evidence ownership changed; refusing cleanup")
        shutil.rmtree(directory)

    test_class.addClassCleanup(cleanup)
    return directory


class LObjPresentationCacheTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = retained_workspace(cls)

    @staticmethod
    def _run(work, name, argv, cwd=None, env=None):
        command = list(map(str, argv))
        try:
            proc = subprocess.run(
                command, cwd=cwd or work, env=env,
                text=True, capture_output=True, timeout=180,
            )
        except subprocess.TimeoutExpired as exc:
            def as_text(value):
                if value is None:
                    return ""
                return value.decode(errors="replace") if isinstance(value, bytes) else value

            (work / f"{name}.json").write_text(json.dumps({
                "argv": command,
                "cwd": str(cwd or work),
                "returncode": None,
                "timed_out": True,
                "timeout_seconds": 180,
                "stdout": as_text(exc.stdout),
                "stderr": as_text(exc.stderr),
            }, indent=2) + "\n")
            raise AssertionError(f"{name} timed out after 180 seconds; output retained") from exc
        (work / f"{name}.json").write_text(json.dumps({
            "argv": command,
            "cwd": str(cwd or work),
            "returncode": proc.returncode,
            "timed_out": False,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
        }, indent=2) + "\n")
        if proc.returncode:
            raise AssertionError(f"{name} failed:\n{proc.stdout}\n{proc.stderr}")
        return proc

    def test_actual_constructor_draw_retirement_and_reuse(self):
        sdk = ROOT / ".deps/emsdk/upstream/emscripten"
        emcc, empp = sdk / "emcc", sdk / "em++"
        if not emcc.is_file() or not empp.is_file():
            self.skipTest("Pinned Emscripten toolchain is unavailable")
        try:
            node = node_runtime(ROOT)
        except (OSError, ValueError) as exc:
            self.skipTest(f"Pinned Node runtime is unavailable: {exc}")

        compile_db_path = os.environ.get("MELEE_LOBJ_CACHE_COMPILE_COMMANDS")
        if compile_db_path:
            compile_db = Path(compile_db_path).resolve()
            if not compile_db.is_file():
                self.fail("Explicit compile-command database is missing")
        else:
            compile_db = ROOT / (
                "build/slippi-synctest-logical-frame-stage-arena-"
                "aurora-staging-release/compile_commands.json"
            )
            if not compile_db.is_file():
                self.skipTest("Existing profiled runtime compile graph is unavailable")
        commands = json.loads(compile_db.read_text())
        class_entries = [entry for entry in commands
                         if entry["file"].endswith("/src/sysdolphin/baselib/class.c")]
        self.assertEqual(len(class_entries), 1, "Actual class compile entry is ambiguous")
        prepared = Path(class_entries[0]["file"]).resolve().parents[2]
        prepared_headers = {}
        for relative in ("Runtime/platform.h", "sysdolphin/baselib/class.h",
                         "sysdolphin/baselib/object.h", "sysdolphin/baselib/lobj.h"):
            header = prepared / relative
            self.assertTrue(header.is_file(), "Actual prepared header is missing: " + str(header))
            prepared_headers[relative] = {"path": str(header), "sha256": sha256(header)}
        (self.work / "prepared-header-inputs.json").write_text(json.dumps({
            "compile_database": str(compile_db), "compile_database_sha256": sha256(compile_db),
            "prepared_root": str(prepared),
            "bool_transform_sha256": sha256(ROOT / "scripts/gameplay_bool.py"),
            "headers": prepared_headers,
        }, indent=2) + "\n")

        melee = ROOT / ".deps/melee"
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        commit = lock["repositories"]["melee"]["commit"]
        upstream_head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=melee, text=True
        ).strip()
        if upstream_head != commit:
            self.fail("Upstream Melee checkout does not match the locked source commit")
        rels = [
            "src/sysdolphin/baselib/class.c",
            "src/sysdolphin/baselib/object.c",
            "src/sysdolphin/baselib/lobj.c",
        ]
        tree = self.work / "melee-source"
        for rel in rels:
            dest = tree / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            raw = subprocess.check_output(
                ["git", "show", f"{commit}:{rel}"], cwd=melee
            )
            dest.write_bytes(raw)

        env = dict(os.environ)
        for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
            env.pop(key, None)
        env["GIT_CEILING_DIRECTORIES"] = str(tree.parent)
        gameplay_patch = ROOT / "patches/melee-gameplay.patch"
        profile_patch = ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"
        include_args = [f"--include={rel}" for rel in rels]
        self._run(self.work, "apply-gameplay-check", [
            "git", "apply", "--check", *include_args, gameplay_patch,
        ], cwd=tree, env=env)
        self._run(self.work, "apply-gameplay", [
            "git", "apply", *include_args, gameplay_patch,
        ], cwd=tree, env=env)
        self._run(self.work, "apply-profile-check", [
            "git", "apply", "--check", *include_args, profile_patch,
        ], cwd=tree, env=env)
        self._run(self.work, "apply-profile", [
            "git", "apply", *include_args, profile_patch,
        ], cwd=tree, env=env)

        # Use the exact token-aware source preparation implementation; the
        # prepared declarations selected below carry its matching int bool ABI.
        for relative in rels:
            path = tree / relative
            path.write_text(transform_source(Path(relative), path.read_text()))

        template = (ROOT / "src/slippi_profile/tests/"
                    "lobj_presentation_owner_control.c.in").read_text()
        control = self.work / "lobj_presentation_owner_control.c"
        control.write_text(template)
        sources = {
            "class": tree / rels[0],
            "object": tree / rels[1],
            "lobj": tree / rels[2],
            "cache": ROOT / "src/slippi_profile/lobj_presentation_cache.c",
            "profile": ROOT / "src/slippi_profile/profile.c",
            "control-c": control,
            "control-cpp": control,
            "ps-math": ROOT / "src/gameplay_ps_math.c",
            "vec": ROOT / ".deps/aurora/lib/dolphin/mtx/vec.c",
            "mtxvec": ROOT / ".deps/aurora/lib/dolphin/mtx/mtxvec.c",
            "gx-lighting": ROOT / ".deps/aurora/lib/dolphin/gx/GXLighting.cpp",
        }
        common_macros = [
            "-DMELEE_WEB_SLIPPI_PROFILE=1",
            "-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=1",
        ]

        def base_args(source_key, mode):
            source = sources[source_key]
            suffix = {
                "class": "/src/sysdolphin/baselib/class.c",
                "object": "/src/sysdolphin/baselib/object.c",
                "lobj": "/src/sysdolphin/baselib/lobj.c",
                "mtxvec": "/mtxvec.c",
                "gx-lighting": "/GXLighting.cpp",
            }.get(source_key)
            args = None
            if suffix:
                entries = [entry for entry in commands
                           if entry["file"].endswith(suffix)]
                if len(entries) != 1:
                    self.fail(f"Expected one actual compiler entry for {suffix}")
                words = shlex.split(entries[0]["command"])
                kept = [words[0]]
                i = 1
                while i < len(words):
                    word = words[i]
                    if word in ("-o", "-c"):
                        i += 2 if word == "-o" else 1
                        if word == "-c" and i < len(words) and words[i] == entries[0]["file"]:
                            i += 1
                        continue
                    if word.startswith("-DMELEE_WEB_"):
                        i += 1
                        continue
                    # Retain the actual production prepared-header search
                    # paths; raw upstream declarations have a different bool ABI.
                    if word == "-include" and i + 1 < len(words):
                        kept.extend([word, str(ROOT / "src/gameplay_compat.h")])
                        i += 2
                        continue
                    kept.append(word)
                    i += 1
                compiler = empp if source_key == "gx-lighting" else emcc
                args = [str(compiler), *kept[1:]]
            else:
                compiler = empp if source_key == "control-cpp" else emcc
                args = [
                    str(compiler), "-O3", "-DNDEBUG",
                    "-DAURORA", "-DAURORA_ENABLE_GX", "-DTARGET_PC",
                    "-ffunction-sections", "-fdata-sections",
                    "-ffp-contract=off",
                    "-I" + str(ROOT / "src"), "-I" + str(prepared),
                    "-I" + str(ROOT / ".deps/aurora/include"),
                    "-include", str(ROOT / "src/gameplay_compat.h"),
                ]
                if source_key == "control-cpp":
                    args[1:1] = ["-std=c++20", "-fexceptions", "-x", "c++"]
                else:
                    args[1:1] = ["-std=gnu11"]
                if source_key == "control-c":
                    args.insert(1, "-x")
                    args.insert(2, "c")
            assert args is not None
            if source_key in ("class", "object", "lobj"):
                # The extracted translation units no longer sit beside their
                # quoted headers, so restore the exact locked upstream directory.
                args.insert(1, "-I" + str(prepared / "sysdolphin/baselib"))
            # Only the active module TU uses the private cache representation.
            args += common_macros
            args += ["-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=" +
                     ("1" if mode == "active" else "0")]
            if source_key == "cache" and mode != "active":
                self.fail("Cache module must not be compiled in the inactive lane")
            return args

        outputs = {}
        for mode in ("active", "inactive"):
            objects = []
            keys = ["class", "object", "lobj", "profile", "control-c",
                    "control-cpp", "ps-math", "vec", "mtxvec", "gx-lighting"]
            if mode == "active":
                keys.append("cache")
            for key in keys:
                source = sources[key]
                target = self.work / f"{mode}-{key}.o"
                args = base_args(key, mode)
                args += ["-c", str(source), "-o", str(target)]
                self._run(self.work, f"compile-{mode}-{key}", args, env=env)
                objects.append(target)
            binary = self.work / f"{mode}-lobj-owner.js"
            link = [
                str(empp), "-std=c++20", "-O1", "-fexceptions",
                "-ffunction-sections", "-fdata-sections",
                "-Wl,--gc-sections", "-Wl,--wrap=malloc", "-Wl,--wrap=free",
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                *map(str, objects), "-o", str(binary),
            ]
            self._run(self.work, f"link-{mode}", link, env=env)
            result = self._run(self.work, f"run-{mode}", [node, binary], env=env)
            outputs[mode] = result.stdout

        def fields(text):
            return dict(re.findall(r"^([A-Z_]+)=([0-9a-f]+)$", text, re.M))

        active = fields(outputs["active"])
        inactive = fields(outputs["inactive"])
        for key in ("SNAP_A", "SNAP_B", "FIFO_SNAP_A", "FIFO_SNAP_B"):
            self.assertIn(key, active)
            self.assertIn(key, inactive)
            self.assertEqual(active[key], inactive[key],
                             f"active presentation projection differs at {key}")
        for mode, result in (("active", active), ("inactive", inactive)):
            snap = bytes.fromhex(result["SNAP_A"])
            fifo = bytes.fromhex(result["FIFO_SNAP_A"])
            self.assertEqual(len(snap), 170)
            self.assertEqual(len(fifo), 138)
            self.assertEqual(int.from_bytes(snap[0:4], "little"), 13)
            self.assertEqual(snap[4:8], bytes((17, 53, 109, 255)))
            self.assertEqual(int.from_bytes(snap[8:12], "little"), 13)
            self.assertEqual(int.from_bytes(snap[32:36], "little"), 1)
            self.assertEqual(int.from_bytes(snap[100:104], "little"), 2)
            self.assertEqual(snap[168:170], b"\0\0")
            self.assertEqual(fifo[0], 0x10)
            self.assertEqual(fifo[69], 0x10)
            self.assertNotEqual(fifo[:69], fifo[69:])
            self.assertRegex(outputs[mode],
                             r"CONTROL_OK .*source_piece_reused=1 .*fifo_a=138 fifo_b=138")
        self.assertIn("slippi-render-dependencies-logical-frame-lobj-v7",
                      outputs["active"])
        self.assertIn("slippi-render-dependencies-logical-frame-v6",
                      outputs["inactive"])
        self.assertRegex(outputs["active"],
                         r"first_token=([1-9][0-9]*) second_token=([1-9][0-9]*)")
        tokens = re.search(r"first_token=(\d+) second_token=(\d+)",
                           outputs["active"])
        self.assertIsNotNone(tokens)
        self.assertNotEqual(tokens.group(1), tokens.group(2))


class LObjCurrentSetV15Tests(OwnedWorkspaceTests):
    """Exercise the real LObj selection bodies with a private current-set view."""

    @classmethod
    def setUpClass(cls):
        cls.work = retained_workspace(cls)

    def test_actual_replace_all_view_matches_source_gx_and_preserves_source(self):
        sdk = ROOT / ".deps/emsdk/upstream/emscripten"
        emcc, empp = sdk / "emcc", sdk / "em++"
        if not emcc.is_file() or not empp.is_file():
            self.skipTest("Pinned Emscripten toolchain is unavailable")
        try:
            node = node_runtime(ROOT)
        except (OSError, ValueError) as exc:
            self.skipTest(f"Pinned Node runtime is unavailable: {exc}")

        compile_db = Path(os.environ.get(
            "MELEE_LOBJ_CACHE_COMPILE_COMMANDS",
            ROOT / ("build/slippi-synctest-logical-frame-stage-arena-"
                    "aurora-staging-release/compile_commands.json"),
        )).resolve()
        if not compile_db.is_file():
            self.skipTest("Existing profiled runtime compile graph is unavailable")
        commands = json.loads(compile_db.read_text())
        class_entries = [entry for entry in commands
                         if entry["file"].endswith("/src/sysdolphin/baselib/class.c")]
        self.assertEqual(len(class_entries), 1, "Actual class compile entry is ambiguous")
        prepared = Path(class_entries[0]["file"]).resolve().parents[2]
        for relative in ("Runtime/platform.h", "sysdolphin/baselib/class.h",
                         "sysdolphin/baselib/object.h", "sysdolphin/baselib/lobj.h"):
            self.assertTrue((prepared / relative).is_file(),
                            "Actual prepared header is missing: " + relative)

        melee = ROOT / ".deps/melee"
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        commit = lock["repositories"]["melee"]["commit"]
        actual_head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=melee, text=True
        ).strip()
        self.assertEqual(actual_head, commit,
                         "Upstream Melee checkout differs from its lock pin")
        rels = ["src/sysdolphin/baselib/class.c",
                "src/sysdolphin/baselib/object.c",
                "src/sysdolphin/baselib/lobj.c"]
        tree = self.work / "current-set-melee-source"
        for relative in rels:
            target = tree / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(subprocess.check_output(
                ["git", "show", f"{commit}:{relative}"], cwd=melee
            ))
        env = dict(os.environ)
        for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
            env.pop(key, None)
        env["GIT_CEILING_DIRECTORIES"] = str(tree.parent)
        include_args = [f"--include={relative}" for relative in rels]
        gameplay_patch = ROOT / "patches/melee-gameplay.patch"
        profile_patch = ROOT / "src/slippi_profile/patches/melee-profile-hooks.patch"
        run = LObjPresentationCacheTests._run
        run(self.work, "v15-apply-gameplay-check",
            ["git", "apply", "--check", *include_args, gameplay_patch], cwd=tree, env=env)
        run(self.work, "v15-apply-gameplay",
            ["git", "apply", *include_args, gameplay_patch], cwd=tree, env=env)
        run(self.work, "v15-apply-profile-check",
            ["git", "apply", "--check", *include_args, profile_patch], cwd=tree, env=env)
        run(self.work, "v15-apply-profile",
            ["git", "apply", *include_args, profile_patch], cwd=tree, env=env)
        for relative in rels:
            path = tree / relative
            path.write_text(transform_source(Path(relative), path.read_text()))

        # Test-only copied-source bridge: read canonical source statics after
        # undefining every V15 lvalue route. Never added to production source.
        lobj_path = tree / rels[2]
        lobj_path.write_text(lobj_path.read_text() + r'''

void slippi_test_lobj_canonical_domain(void **head, void **active, int *count, int *masks)
{
    int i;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#undef current_lights
#undef active_lights
#undef nb_active_lights
#undef lightmask_diffuse
#undef lightmask_specular
#undef lightmask_attnfunc
#undef lightmask_alpha
#endif
    *head = current_lights;
    for (i = 0; i < MAX_GXLIGHT; ++i) active[i] = active_lights[i];
    *count = nb_active_lights;
    masks[0] = lightmask_diffuse; masks[1] = lightmask_specular;
    masks[2] = lightmask_attnfunc; masks[3] = lightmask_alpha;
}
''')

        control = self.work / "lobj_current_set_v15_control.cpp"
        control.write_text((ROOT / "src/slippi_profile/tests/"
                            "lobj_current_set_v15_control.c.in").read_text())
        sources = {
            "class": tree / rels[0], "object": tree / rels[1],
            "lobj": tree / rels[2],
            "cache": ROOT / "src/slippi_profile/lobj_presentation_cache.c",
            "profile": ROOT / "src/slippi_profile/profile.c",
            "control": control,
            "ps-math": ROOT / "src/gameplay_ps_math.c",
            "vec": ROOT / ".deps/aurora/lib/dolphin/mtx/vec.c",
            "mtxvec": ROOT / ".deps/aurora/lib/dolphin/mtx/mtxvec.c",
            "gx-lighting": ROOT / ".deps/aurora/lib/dolphin/gx/GXLighting.cpp",
        }
        common = ["-DMELEE_WEB_SLIPPI_PROFILE=1",
                  "-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=1",
                  "-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=1"]

        def compiler_args(key, v15):
            source = sources[key]
            suffix = {"class": "/src/sysdolphin/baselib/class.c",
                      "object": "/src/sysdolphin/baselib/object.c",
                      "lobj": "/src/sysdolphin/baselib/lobj.c",
                      "mtxvec": "/mtxvec.c",
                      "gx-lighting": "/GXLighting.cpp"}.get(key)
            if suffix:
                entries = [entry for entry in commands
                           if entry["file"].endswith(suffix)]
                self.assertEqual(len(entries), 1,
                                 "Expected one compiler entry for " + suffix)
                words = shlex.split(entries[0]["command"])
                kept = [words[0]]
                i = 1
                while i < len(words):
                    word = words[i]
                    if word in ("-o", "-c"):
                        i += 2 if word == "-o" else 1
                        if word == "-c" and i < len(words) and words[i] == entries[0]["file"]:
                            i += 1
                        continue
                    if word.startswith("-DMELEE_WEB_"):
                        i += 1
                        continue
                    if word == "-include" and i + 1 < len(words):
                        kept.extend([word, str(ROOT / "src/gameplay_compat.h")])
                        i += 2
                        continue
                    kept.append(word)
                    i += 1
                args = [str(empp if key == "gx-lighting" else emcc), *kept[1:]]
            else:
                args = [str(empp if key == "control" else emcc),
                        "-O3", "-DNDEBUG", "-DAURORA", "-DAURORA_ENABLE_GX",
                        "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
                        "-ffp-contract=off", "-I" + str(ROOT / "src"),
                        "-I" + str(prepared), "-I" + str(ROOT / ".deps/aurora/include"),
                        "-include", str(ROOT / "src/gameplay_compat.h")]
                if key == "control":
                    args[1:1] = ["-std=c++20", "-fexceptions", "-x", "c++"]
                else:
                    args[1:1] = ["-std=gnu11"]
            if key in ("class", "object", "lobj"):
                args.insert(1, "-I" + str(prepared / "sysdolphin/baselib"))
            args += common
            args.append("-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=" +
                        ("1" if v15 else "0"))
            return args

        output = {}
        for lane, v15 in (("source-reference", False), ("v15", True)):
            objects = []
            for key in ("class", "object", "lobj", "profile", "cache", "control",
                        "ps-math", "vec", "mtxvec", "gx-lighting"):
                obj = self.work / f"{lane}-{key}.o"
                run(self.work, f"v15-compile-{lane}-{key}",
                    [*compiler_args(key, v15), "-c", str(sources[key]), "-o", str(obj)],
                    env=env)
                objects.append(obj)
            binary = self.work / f"{lane}-current-set.js"
            run(self.work, f"v15-link-{lane}",
                [str(empp), "-std=c++20", "-O1", "-fexceptions",
                 "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                 "-Wl,--wrap=malloc", "-Wl,--wrap=free", "-sENVIRONMENT=node",
                 "-sEXIT_RUNTIME=1", *map(str, objects), "-o", str(binary)], env=env)
            output[lane] = run(self.work, f"v15-run-{lane}",
                               [node, binary], env=env).stdout
            if v15:
                for case in ("noref", "sole-member", "overflow", "class-change",
                             "foreign-chain", "wrong-token-clean"):
                    result = run(self.work, f"v15-run-{lane}-{case}",
                                 [node, binary, case], env=env).stdout
                    self.assertIn(f"CONTROL_OK_CURRENT_SET_{case.upper().replace('-', '_')}",
                                  result)

        def field(name, output_text):
            match = re.search(rf"^{name}=([0-9a-f]*)$", output_text, re.M)
            self.assertIsNotNone(match, "Missing control output " + name)
            return match.group(1)

        self.assertEqual(field("FIFO_REPLACED", output["source-reference"]),
                         field("FIFO_REPLACED", output["v15"]),
                         "V15 current-set ReplaceAll changed actual GX output")
        self.assertIn("slippi-render-dependencies-logical-frame-lobj-tobj-"
                      "current-light-set-v15", output["v15"])
        self.assertIn("CONTROL_OK_CURRENT_SET", output["v15"])
        self.assertIn("CURRENT_SET_PERSISTED_SELECTION=1", output["v15"])
        self.assertIn("CURRENT_SET_RESET_SELECTION=1", output["v15"])
        self.assertRegex(output["v15"], r"ref_first=[1-9][0-9]* ref_second=[1-9][0-9]*")


if __name__ == "__main__":
    unittest.main()
