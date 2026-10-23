"""Compile and run the actual ActionStore interval boundary in a Wasm32 fixture."""
import ast
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
        target = candidate_src / relative if relative.as_posix() in candidate_names else source
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
        sys.path.insert(0, str(REPO_ROOT / "scripts"))
        from gameplay_sources import prepare_sources
        source = prepare_sources(REPO_ROOT)

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
        shutil.rmtree(output_dir)


if __name__ == "__main__":
    unittest.main()
