# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual particle sort/update order control using the existing Node runtime."""
from pathlib import Path
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
MELEE = ROOT / ".deps/melee"
BUILD = ROOT / "build/slippi-synctest-logical-frame-lobj-presentation-tobj-presentation-stage-arena-aurora-staging-release"
PROFILE = ROOT / "src/slippi_profile"
_saved_sys_path = sys.path.copy()
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
sys.path[:] = _saved_sys_path
EXPECTED = {
    "src/sysdolphin/baselib/psdisp.c": "f5aeb59cb2510213053d03819354424166677d75b44e5ab671165a75832b51af",
    "src/sysdolphin/baselib/psstructs.h": "2be98a2a54ac96e8687bcfa3baf39bffc5308f991c7a668b2cfb10cd46a27bc4",
    "src/sysdolphin/baselib/psappsrt.c": "7ed3ff2a4d270a51e06f6ff86961d6ec1ee4d7510d11e33eb06e023db86561db",
    "src/sysdolphin/baselib/particle.c": "12857a1954beef2167cbd4160ff5592487273462fecdae651df406d679a7af8f",
    "src/sysdolphin/baselib/random.c": "c1ee9c4317a48b59021c23b901e97fe7ad794f148d6ab98783ecfcadcdd566e6",
    "src/melee/ef/eflib.c": "5450ecd99821f09ea4b9706d400d84b9a42d9d55d2864f530b2a98ce7481dbd4",
}


class ParticleRenderKeyDependencyTests(unittest.TestCase):
    def test_real_sort_changes_actual_successor_particle_rng_assignment(self):
        if not (BUILD / "libfighter_source_runtime.a").is_file():
            self.skipTest("existing source-runtime build is unavailable; this control does not build it")
        for rel, expected in EXPECTED.items():
            digest = hashlib.sha256((MELEE / rel).read_bytes()).hexdigest()
            self.assertEqual(digest, expected, rel)

        output_root = Path(os.environ.get(
            "MELEE_PARTICLE_RENDER_KEY_OUTPUT_ROOT", ROOT / "work"))
        output_root.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="particle-render-key-", dir=output_root))
        try:
            control = scratch / "control.cpp"
            control.write_text(
                (PROFILE / "tests/particle_render_key_control.cpp.in").read_text(),
                encoding="utf-8")
            emxx = ROOT / ".deps/emsdk/upstream/emscripten/em++"
            common = [str(emxx), "-std=gnu++17", "-O1", "-fexceptions",
                      "-ffp-contract=off", "-DTARGET_PC", "-DAURORA",
                      "-I" + str(ROOT / "src"),
                      "-I" + str(ROOT / "build/gameplay-source-slippi-profile/src"),
                      "-I" + str(ROOT / ".deps/aurora/include"),
                      "-I" + str(ROOT / ".deps/melee/extern/dolphin/include"),
                      "-include", str(ROOT / "src/gameplay_compat.h"),
                      "-Wno-writable-strings"]
            object_file = scratch / "control.o"
            compile_command = common + ["-c", str(control), "-o", str(object_file)]
            self._run("compile", compile_command, ROOT, scratch, 90)

            commands = subprocess.check_output(
                [str(ROOT / ".venv/bin/ninja"), "-C", str(BUILD), "-t", "commands",
                 "gameplay_slippi_synctest_node"], cwd=ROOT, text=True).splitlines()
            links = [line for line in commands
                     if "gameplay_slippi_synctest_node.js" in line and " -o " in line]
            self.assertEqual(len(links), 1, "expected exactly one existing Node link command")
            (scratch / "official-node-link-command.txt").write_text(
                links[0] + "\n", encoding="utf-8")
            tokens = shlex.split(links[0])
            if tokens[:2] == [":", "&&"]:
                tokens = tokens[2:]
            if tokens[-2:] == ["&&", ":"]:
                tokens = tokens[:-2]
            linked = []
            i = 0
            while i < len(tokens):
                token = tokens[i]
                if (token == "--no-entry" or token == "-sMODULARIZE=1" or
                        token.startswith("-sEXPORT_NAME=") or
                        token.startswith("-sEXPORTED_FUNCTIONS=")):
                    i += 1
                    continue
                if "CMakeFiles/gameplay_slippi_synctest_node.dir/" in token and token.endswith(".o"):
                    i += 1
                    continue
                if token == "-o":
                    linked.extend([token, str(scratch / "control.js")])
                    i += 2
                    continue
                if token == "-sEXIT_RUNTIME=0":
                    linked.append("-sEXIT_RUNTIME=1")
                    i += 1
                    continue
                linked.append(token)
                i += 1
            self.assertIn("-o", linked)
            out_index = linked.index("-o")
            linked.insert(out_index, str(object_file))
            linked.insert(out_index + 1, "-sEXPORTED_FUNCTIONS=_main")
            (scratch / "control-link-command.txt").write_text(
                shlex.join(linked) + "\n", encoding="utf-8")
            self._run("link", linked, BUILD, scratch, 180)

            node = node_runtime(ROOT)
            result = self._run("run", [str(node), str(scratch / "control.js")],
                               ROOT, scratch, 30)
            self.assertIn("draw-sorted-before-update order=3,1,2,", result.stdout)
            self.assertIn("isolated-source-before-update order=1,2,3,", result.stdout)
            self.assertIn("draw-sorted-after-update order=3,1,2, lives=1:170;2:458;3:639;",
                          result.stdout)
            self.assertIn("isolated-source-after-update order=1,2,3, lives=1:239;2:370;3:658;",
                          result.stdout)
            self.assertIn("rng_after_sorted=cc0ddcad rng_after_isolated=cc0ddcad",
                          result.stdout)
            self.assertIn("actual successor particle-state dependency reproduced",
                          result.stdout)
            input_hashes = []
            hash_cache = {}
            for token in linked:
                if not token.endswith((".a", ".o")):
                    continue
                path = Path(token)
                if not path.is_absolute():
                    path = BUILD / path
                path = path.resolve(strict=True)
                digest = hash_cache.get(path)
                if digest is None:
                    digest = hashlib.sha256(path.read_bytes()).hexdigest()
                    hash_cache[path] = digest
                input_hashes.append({
                    "path": str(path),
                    "sha256": digest,
                    "size": path.stat().st_size,
                    "kind": "object" if path.suffix == ".o" else "archive",
                })
            input_paths = {entry["path"] for entry in input_hashes}
            self.assertIn(str(object_file.resolve()), input_paths)
            self.assertIn(str((BUILD / "libfighter_source_runtime.a").resolve()),
                          input_paths)
            self.assertIn(str((BUILD / "libhsd_native_runtime.a").resolve()),
                          input_paths)

            source_runtime = BUILD / "libfighter_source_runtime.a"
            hsd_runtime = BUILD / "libhsd_native_runtime.a"
            (scratch / "pins.json").write_text(json.dumps({
                "repository_head": subprocess.check_output(
                    ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "repository_root": str(ROOT),
                "test_scope": "actual source particleSort + actual successor update/RNG control; no production edits",
                "fixture_source_sha256": hashlib.sha256(control.read_bytes()).hexdigest(),
                "control_object": {
                    "path": str(object_file.resolve()),
                    "sha256": hashlib.sha256(object_file.read_bytes()).hexdigest(),
                    "size": object_file.stat().st_size,
                },
                "generated_control_outputs": {
                    name: {
                        "path": str((scratch / name).resolve()),
                        "sha256": hashlib.sha256((scratch / name).read_bytes()).hexdigest(),
                        "size": (scratch / name).stat().st_size,
                    }
                    for name in ("control.js", "control.wasm")
                },
                "node_runtime": {
                    "path": str(node),
                    "sha256": hashlib.sha256(node.read_bytes()).hexdigest(),
                },
                "official_node_link_command_sha256": hashlib.sha256(
                    links[0].encode()).hexdigest(),
                "linked_inputs_in_command_order": input_hashes,
                "upstream_source_sha256": EXPECTED,
                "prepared_source_sha256": {
                    "particle.c": hashlib.sha256(
                        (ROOT / "build/gameplay-source-slippi-profile/src/sysdolphin/baselib/particle.c").read_bytes()).hexdigest(),
                    "psdisp.c": hashlib.sha256(
                        (ROOT / "build/gameplay-source-slippi-profile/src/sysdolphin/baselib/psdisp.c").read_bytes()).hexdigest(),
                },
                "prepared_particle_source_sha256": hashlib.sha256(
                    (ROOT / "build/gameplay-source-slippi-profile/src/sysdolphin/baselib/particle.c").read_bytes()).hexdigest(),
                "prepared_psdisp_source_sha256": hashlib.sha256(
                    (ROOT / "build/gameplay-source-slippi-profile/src/sysdolphin/baselib/psdisp.c").read_bytes()).hexdigest(),
                "source_runtime_archive_sha256": hashlib.sha256(
                    source_runtime.read_bytes()).hexdigest(),
                "hsd_native_runtime_archive_sha256": hashlib.sha256(
                    hsd_runtime.read_bytes()).hexdigest(),
                "archive_provenance": "pre-existing Node-target build output; this focused test linked it without rebuilding it and does not attribute it to the current repository HEAD",
                "fixture_output": result.stdout,
            }, indent=2) + "\n", encoding="utf-8")
            self.assertTrue((scratch / "pins.json").is_file())
        except BaseException:
            print("Retained actual particle control:", scratch, flush=True)
            raise
        else:
            # Retain complete provenance in an explicitly requested external
            # evidence root. Successful disposable runs without one clean up.
            evidence = Path(os.environ.get(
                "MELEE_PARTICLE_RENDER_KEY_EVIDENCE",
                output_root / "particle-render-key-control-result.json"))
            evidence.parent.mkdir(parents=True, exist_ok=True)
            evidence.write_bytes((scratch / "pins.json").read_bytes())
            keep_all = os.environ.get("MELEE_KEEP_TEST_ARTIFACTS") == "1"
            retain_receipt = "MELEE_PARTICLE_RENDER_KEY_OUTPUT_ROOT" in os.environ
            if keep_all:
                print(f"Retained particle control artifacts: {scratch}", flush=True)
            elif retain_receipt:
                for name in ("control.o", "control.js", "control.wasm"):
                    path = scratch / name
                    if path.exists():
                        path.unlink()
                print(f"Retained particle control receipt: {scratch}", flush=True)
            else:
                import shutil
                shutil.rmtree(scratch)

    def _run(self, name, command, cwd, scratch, timeout):
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                                timeout=timeout)
        (scratch / (name + ".json")).write_text(json.dumps({
            "argv": list(map(str, command)), "cwd": str(cwd),
            "exit_code": result.returncode, "stdout": result.stdout,
            "stderr": result.stderr,
        }, indent=2) + "\n", encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result


if __name__ == "__main__":
    unittest.main()
