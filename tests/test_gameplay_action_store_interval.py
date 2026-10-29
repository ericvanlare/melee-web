"""Compile and run the actual ActionStore interval boundary in a Wasm32 fixture."""
import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


REPO_ROOT = Path(os.environ.get("MELEE_ACTION_STORE_REPO_ROOT", Path(__file__).resolve().parents[1])).resolve()
CANDIDATE_ROOT = Path(os.environ.get("MELEE_ACTION_STORE_CANDIDATE_ROOT", Path(__file__).resolve().parents[1])).resolve()
TEST_OUTPUT_ROOT = Path(os.environ.get(
    "MELEE_ACTION_STORE_TEST_OUTPUT", CANDIDATE_ROOT / "focused-controls")).resolve()


def command_record(path, command, result):
    path.write_text(json.dumps({
        "command": [str(part) for part in command],
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }, indent=2) + "\n")


def first_difference(left, right, path="$"):
    if type(left) is not type(right):
        return path, left, right
    if isinstance(left, dict):
        keys = sorted(set(left) | set(right))
        for key in keys:
            if key not in left or key not in right:
                return f"{path}.{key}", left.get(key, "<missing>"), right.get(key, "<missing>")
            difference = first_difference(left[key], right[key], f"{path}.{key}")
            if difference:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}.length", len(left), len(right)
        for index, (a, b) in enumerate(zip(left, right)):
            difference = first_difference(a, b, f"{path}[{index}]")
            if difference:
                return difference
        return None
    return None if left == right else (path, left, right)


def make_source_overlay(output_dir):
    """Resolve quoted project includes against the candidate's eight owned files."""
    overlay = output_dir / "src"
    candidate_src = CANDIDATE_ROOT / "src"
    candidate_names = {
        "gameplay_action_store.h",
        "gameplay_action_store.c",
        "gameplay_action_store.hpp",
        "gameplay_action_store.cpp",
        "dat_fighter_runtime.hpp",
        "dat_fighter_runtime.cpp",
    }
    for source in (REPO_ROOT / "src").rglob("*"):
        relative = source.relative_to(REPO_ROOT / "src")
        destination = overlay / relative
        if source.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        candidate = candidate_src / relative
        target = candidate if relative.as_posix() in candidate_names and candidate.is_file() else source
        destination.symlink_to(target)
    return overlay


class GameplayActionStoreIntervalTests(unittest.TestCase):
    def test_generic_dat_runtime_links_without_gameplay_action_store(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required for the generic dat-only control")
        TEST_OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
        output_dir = Path(tempfile.mkdtemp(prefix="action-store-generic-dat-", dir=TEST_OUTPUT_ROOT))
        overlay_src = make_source_overlay(output_dir)
        binary = output_dir / "fighter_runtime_test"
        sources = [overlay_src / (name + ".cpp") for name in
                   ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")]
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                   "-I", str(overlay_src), *map(str, sources),
                   str(REPO_ROOT / "tests/dat_fighter_runtime_test.cpp"), "-o", str(binary)]
        result = subprocess.run(command, cwd=output_dir, capture_output=True, text=True, timeout=120)
        command_record(output_dir / "generic-dat-link.json", command, result)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for case in ("decoded_values", "malformed_attributes", "malformed_actions",
                     "owned_command_boundary", "selected_motion_identity", "hurtbox_dynamics"):
            run = [str(binary), case]
            result = subprocess.run(run, cwd=output_dir, capture_output=True, text=True, timeout=20)
            command_record(output_dir / f"generic-dat-{case}.json", run, result)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        print("Generic dat-only runtime linked and passed six controls without gameplay ActionStore objects.")
        shutil.rmtree(output_dir)

    def test_actual_source_interval_and_refusals(self):
        sdk = REPO_ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler / "emcc.py").is_file() or not config.is_file():
            self.skipTest("Project SDK unavailable; interval controls require the pinned Wasm toolchain")
        node_setting = None
        for statement in ast.parse(config.read_text()).body:
            if (isinstance(statement, ast.Assign) and
                    any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets)):
                node_setting = ast.literal_eval(statement.value)
        self.assertIsInstance(node_setting, str)
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        lock = json.loads((REPO_ROOT / "dependencies.lock.json").read_text())
        self.assertEqual((compiler / "emscripten-version.txt").read_text().strip().strip('"'), lock["emscripten"])
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        # Unit controls use existing prepared dependencies read-only. Source
        # preparation belongs to the supported build entry point, never a test.
        source = REPO_ROOT / "build/gameplay-source/src"
        if not (source / "melee/ft/forward.h").is_file():
            self.skipTest("Existing generated source headers unavailable; prepare through the supported build entry point")
        generated_head=subprocess.run(["git","rev-parse","HEAD"],cwd=source,
            text=True,capture_output=True,check=True).stdout.strip()
        self.assertEqual(generated_head,lock["repositories"]["melee"]["commit"])

        TEST_OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
        output_dir = Path(tempfile.mkdtemp(prefix="action-store-interval-", dir=TEST_OUTPUT_ROOT))
        output = output_dir / "action_store_interval.js"
        overlay_src = make_source_overlay(output_dir)
        include_dirs = [
            overlay_src,
            REPO_ROOT / ".deps/aurora/include",
            source,
            REPO_ROOT / "tests",
        ]
        common = ["-O1", "-fexceptions", "-DTARGET_PC", "-DMELEE_WEB_ACTION_INTERVAL_TEST",
                  "-ffunction-sections", "-fdata-sections", "-ffp-contract=off"]
        for directory in include_dirs:
            common.extend(["-I", str(directory)])
        common.extend(["-include", str(REPO_ROOT / "src/gameplay_compat.h")])
        commands = []
        c_sources = [overlay_src / "gameplay_action_store.c"]
        cpp_sources = [
            overlay_src / "dat_archive.cpp",
            overlay_src / "dat_animation.cpp",
            overlay_src / "fighter_binding.cpp",
            overlay_src / "dat_fighter_runtime.cpp",
            overlay_src / "dat_commands.cpp",
            overlay_src / "gameplay_result_motion_table.cpp",
            overlay_src / "gameplay_action_store.cpp",
            CANDIDATE_ROOT / "tests/action_store_interval_control.cpp",
        ]
        for index, path in enumerate(c_sources):
            commands.append([sys.executable, str(compiler / "emcc.py"), *common,
                "-std=c11", "-c", str(path), "-o", str(output_dir / f"c-{index}.o")])
        for index, path in enumerate(cpp_sources):
            commands.append([sys.executable, str(compiler / "em++.py"), *common,
                "-std=c++20", "-c", str(path), "-o", str(output_dir / f"cpp-{index}.o")])
        for index, command in enumerate(commands):
            result = subprocess.run(command, cwd=output_dir, env=env, capture_output=True, text=True, timeout=120)
            command_record(output_dir / f"compile-{index}.json", command, result)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        link = [sys.executable, str(compiler / "em++.py"), "-O1", "-fexceptions",
                *map(str, output_dir.glob("*.o")), "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-sALLOW_MEMORY_GROWTH=1", "-o", str(output)]
        result = subprocess.run(link, cwd=output_dir, env=env, capture_output=True, text=True, timeout=120)
        command_record(output_dir / "link.json", link, result)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(node), str(output)], cwd=output_dir, env=env,
                                capture_output=True, text=True, timeout=30)
        command_record(output_dir / "run.json", [str(node), str(output)], result)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("ActionStore interval actual-source controls passed", result.stdout)
        print(result.stdout, end="")

        # Each branch starts a separate Node process over this exact Wasm
        # binary, giving both branches a fresh, identically initialized module
        # and linear-memory layout. The C++ lane arguments have equal length;
        # all lane-dependent work begins after the common prestate witness.
        lane_results = {}
        for lane in ("gate-a", "gate-b"):
            command = [str(node), str(output), lane]
            run = subprocess.run(command, cwd=output_dir, env=env,
                                 capture_output=True, text=True, timeout=30)
            command_record(output_dir / f"run-{lane}.json", command, run)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            try:
                lane_results[lane] = json.loads(run.stdout)
            except json.JSONDecodeError as error:
                self.fail(f"{lane} did not emit the bounded JSON witness: {error}\n{run.stdout}\n{run.stderr}")

        clean = lane_results["gate-a"]
        corrected = lane_results["gate-b"]
        self.assertEqual(clean["lane"], "gate-a")
        self.assertEqual(corrected["lane"], "gate-b")
        self.assertEqual(clean["actions"], corrected["actions"])
        self.assertEqual(clean["prestate"]["p1"]["motion"], 14)
        self.assertEqual(clean["prestate"]["p2"]["motion"], 14)
        prestate_difference = first_difference(
            {"layout": clean["layout"], "prestate": clean["prestate"]},
            {"layout": corrected["layout"], "prestate": corrected["prestate"]})
        self.assertIsNone(
            prestate_difference,
            "Fresh Wasm instances did not establish identical prestate/layout: "
            f"{prestate_difference}")
        self.assertEqual(corrected["branch_a"]["witness"]["motion"], corrected["actions"]["A"]["motion"])
        self.assertNotEqual(corrected["branch_a"]["witness"]["row_identity"],
                            corrected["endpoint_p2"]["witness"]["row_identity"])
        lifecycle = corrected["lifecycle"]
        self.assertTrue(lifecycle["a_lease_unchanged_after_restore"])
        self.assertTrue(lifecycle["a_lease_unchanged_after_disarm"])
        self.assertTrue(lifecycle["a_lease_unchanged_after_owner_retirement"])
        self.assertIsNone(first_difference(corrected["branch_a"]["witness"],
                                           corrected["branch_a_after_restore"]["witness"],
                                           "$.branch_a_after_restore"))
        self.assertIsNone(first_difference(corrected["branch_a"]["witness"],
                                           corrected["branch_a_after_owner_retirement"]["witness"],
                                           "$.branch_a_after_owner_retirement"))
        self.assertIsNone(first_difference(corrected["branch_a"]["witness"],
                                           corrected["branch_a_after_store_destruction"]["witness"],
                                           "$.branch_a_after_store_destruction"))
        self.assertEqual(lifecycle["destroys_after_a"], lifecycle["destroys_after_restore"])
        self.assertEqual(lifecycle["destroys_after_restore"], lifecycle["destroys_after_disarm"])
        self.assertEqual(clean["lifecycle"]["creates_after_endpoint"],
                         clean["lifecycle"]["creates_before_branch"],
                         "Clean resident selections allocated Clips after preparation")
        self.assertEqual(lifecycle["creates_after_a"], lifecycle["creates_before_branch"],
                         "Predicted resident selection allocated a Clip after preparation")
        self.assertEqual(lifecycle["creates_after_endpoint"], lifecycle["creates_before_branch"],
                         "Corrected resident selection allocated a Clip after preparation")
        self.assertEqual(clean["lifecycle"]["destroys_after_owner_retirement"],
                         clean["lifecycle"]["destroys_after_endpoint"],
                         "Unbind released a Clip still owned by the resident source catalog")
        self.assertEqual(lifecycle["destroys_after_owner_retirement"],
                         lifecycle["destroys_after_endpoint"],
                         "Unbind released a Clip still owned by the resident source catalog")
        self.assertGreater(lifecycle["destroys_after_capsule_destruction"],
                           lifecycle["destroys_after_store_destruction"],
                           "Destroying the capsule did not release its retained A Clip lease")

        print("ActionStore retention gate: identical fresh-Wasm prestate/layout verified.")
        difference = first_difference(clean["endpoint_p2"], corrected["endpoint_p2"], "$.endpoint_p2")
        result_path = output_dir / "retention-gate-comparison.json"
        wasm_path = output.with_suffix(".wasm")
        comparison = {
            "prestate_layout_match": True,
            "endpoint_status": "PASS" if difference is None else "SOURCE_BORROWED_DIVERGENCE",
            "first_endpoint_difference": None if difference is None else {
                "path": difference[0], "clean": difference[1], "retained_a_then_b": difference[2]},
            "javascript_wrapper_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "wasm_module_sha256": hashlib.sha256(wasm_path.read_bytes()).hexdigest(),
            "clean_lifecycle": clean["lifecycle"],
            "retained_a_lifecycle": corrected["lifecycle"],
        }
        result_path.write_text(json.dumps(comparison, indent=2) + "\n")
        if difference is None:
            print("ACTION_STORE_RETENTION_GATE=PASS: clean B and retained-A-then-B borrowed identities/bytes match.")
        else:
            path, clean_value, corrected_value = difference
            print("ACTION_STORE_RETENTION_GATE=SOURCE_BORROWED_DIVERGENCE")
            print(f"First endpoint difference at {path}: clean={clean_value!r}; retained-A-then-B={corrected_value!r}")
            self.fail("Ordered ActionStore retention identity gate found a source-borrowed endpoint divergence; "
                      f"retained comparison: {result_path}")
        print(f"JavaScript wrapper SHA-256: {comparison['javascript_wrapper_sha256']}")
        print(f"Wasm module SHA-256: {comparison['wasm_module_sha256']}")
        preserve_output = os.environ.get("MELEE_ACTION_STORE_PRESERVE_OUTPUT") == "1"
        if preserve_output:
            print(f"Retained ActionStore gate artifacts: {output_dir}")
        else:
            shutil.rmtree(output_dir)


if __name__ == "__main__":
    unittest.main()
