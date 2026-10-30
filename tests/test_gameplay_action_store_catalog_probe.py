"""Probe the complete authored Mario resident action catalog from actual DATs."""
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import unittest

from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]
ASSET_ENV = "MELEE_ACTION_STORE_CATALOG_ASSETS"
OUTPUT_ENV = "MELEE_ACTION_STORE_CATALOG_OUTPUT"
CORE_FREEZE_ENV = "MELEE_ACTION_STORE_CATALOG_CORE_FREEZE"
CORE_FREEZE_SHA_ENV = "MELEE_ACTION_STORE_CATALOG_CORE_FREEZE_SHA256"
SOURCE_NAMES = (
    "dat_archive.cpp",
    "dat_animation.cpp",
    "fighter_binding.cpp",
    "dat_fighter_runtime.cpp",
    "dat_commands.cpp",
    "gameplay_result_motion_table.cpp",
    "gameplay_action_store.cpp",
)
PINNED_SOURCE_FILES = (
    "src/dat_archive.hpp",
    "src/dat_archive.cpp",
    "src/dat_animation.hpp",
    "src/dat_animation.cpp",
    "src/fighter_binding.hpp",
    "src/fighter_binding.cpp",
    "src/dat_fighter_runtime.hpp",
    "src/dat_fighter_runtime.cpp",
    "src/dat_commands.hpp",
    "src/dat_commands.cpp",
    "src/gameplay_action_store.h",
    "src/gameplay_action_store.hpp",
    "src/gameplay_action_store.c",
    "src/gameplay_action_store.cpp",
    "src/gameplay_result_motion_table.hpp",
    "src/gameplay_result_motion_table.cpp",
    "src/gameplay_compat.h",
    "tests/gameplay_action_store_catalog_probe.cpp",
    "tests/test_gameplay_action_store_catalog_probe.py",
    "tests/action_store_interval_control.cpp",
    "tests/test_gameplay_action_store_interval.py",
    "tests/owned_test_workspace.py",
    "scripts/check_gameplay.py",
    "scripts/bootstrap.py",
    "scripts/workspace_resources.py",
    "scripts/gameplay_sources.py",
    "scripts/build.py",
    "patches/melee-gameplay.patch",
    "dependencies.lock.json",
)
FROZEN_CORE_PRODUCTION_FILES = (
    "src/dat_fighter_runtime.cpp",
    "src/dat_fighter_runtime.hpp",
    "src/gameplay_action_store.cpp",
    "src/gameplay_action_store.hpp",
)
FROZEN_CORE_CONTROL_FILES = (
    "tests/action_store_interval_control.cpp",
    "tests/test_gameplay_action_store_interval.py",
)
PROBE_SOURCE_FILES = (
    "tests/gameplay_action_store_catalog_probe.cpp",
    "tests/test_gameplay_action_store_catalog_probe.py",
)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_receipt(path, command, result):
    path.write_text(json.dumps({
        "command": [str(part) for part in command],
        "returncode": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }, indent=2) + "\n")


def run_command(output, label, command, *, cwd, env, timeout):
    try:
        result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                                text=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        def text_value(value):
            if isinstance(value, bytes):
                return value.decode(errors="replace")
            return value or ""
        (output / f"{label}.json").write_text(json.dumps({
            "command": [str(part) for part in command],
            "returncode": None,
            "timeout_seconds": timeout,
            "stdout": text_value(error.stdout),
            "stderr": text_value(error.stderr),
        }, indent=2) + "\n")
        raise AssertionError(f"{label} exceeded its {timeout}s bound; receipt retained") from error
    command_receipt(output / f"{label}.json", command, result)
    if result.returncode:
        raise AssertionError(
            f"{label} failed with {result.returncode}; stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


class GameplayActionStoreCatalogProbeTests(OwnedWorkspaceTests):
    def test_actual_mario_complete_resident_catalog(self):
        supplied_assets = os.environ.get(ASSET_ENV)
        if supplied_assets is None:
            self.skipTest(f"Set {ASSET_ENV} to run the private actual-Mario asset probe")

        assets = Path(supplied_assets).expanduser().resolve(strict=True)
        self.assertTrue(assets.is_dir(), f"Explicit {ASSET_ENV} must be a directory")
        fighter = assets / "PlMr.dat"
        animations = assets / "PlMrAJ.dat"
        for path in (fighter, animations):
            self.assertTrue(path.is_file(), f"Explicit Mario asset input is missing: {path}")
            self.assertGreater(path.stat().st_size, 0, f"Mario asset input is empty: {path}")

        freeze_name = os.environ.get(CORE_FREEZE_ENV)
        freeze_sha = os.environ.get(CORE_FREEZE_SHA_ENV)
        self.assertIsNotNone(freeze_name, f"Explicit actual-source runs require {CORE_FREEZE_ENV}")
        self.assertIsNotNone(freeze_sha, f"Explicit actual-source runs require {CORE_FREEZE_SHA_ENV}")
        core_freeze = Path(freeze_name).expanduser().resolve(strict=True)
        self.assertEqual(sha256(core_freeze), freeze_sha.lower(),
                         "Core freeze receipt SHA256 differs from the supplied pin")
        frozen_core = json.loads(core_freeze.read_text())
        expected_core = {entry["path"]: entry["sha256"]
                         for entry in frozen_core.get("source_changes", [])}
        self.assertEqual(set(expected_core),
                         set(FROZEN_CORE_PRODUCTION_FILES) | set(FROZEN_CORE_CONTROL_FILES),
                         "Core freeze did not pin the expected catalog implementation/control set")
        for name in FROZEN_CORE_PRODUCTION_FILES:
            expected = expected_core[name]
            self.assertEqual(sha256(ROOT / name), expected,
                             f"Core source differs from the frozen gate: {name}")

        explicit_output = os.environ.get(OUTPUT_ENV)
        if explicit_output is not None:
            output = Path(explicit_output).expanduser().absolute()
            output = output.parent.resolve(strict=True) / output.name
            self.assertFalse(output.exists(), f"Evidence output must be new: {output}")
            self.assertTrue(output.parent.is_dir(), f"Evidence parent must exist: {output.parent}")
            self.assertFalse(output.is_symlink(), "Evidence output cannot be a symlink")
            self.assertFalse(output.is_relative_to(ROOT), "Explicit evidence must stay outside the checkout")

        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler / "emcc.py").is_file() or not (compiler / "em++.py").is_file() or not config.is_file():
            self.skipTest("Pinned project Emscripten SDK unavailable; use the supported bootstrap/build entry point")
        sys.path.insert(0, str(ROOT / "scripts"))
        from check_gameplay import node_runtime

        node = node_runtime(ROOT)
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        self.assertEqual((compiler / "emscripten-version.txt").read_text().strip().strip('"'),
                         lock["emscripten"])
        source = ROOT / "build/gameplay-source/src"
        if not (source / "melee/ft/forward.h").is_file():
            self.skipTest("Pinned prepared source headers unavailable; prepare through scripts/build.py")
        source_head = subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=source, capture_output=True, text=True,
            timeout=10, check=True).stdout.strip()
        self.assertEqual(source_head, lock["repositories"]["melee"]["commit"])
        source_status = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=all"], cwd=source, capture_output=True,
            text=True, timeout=10, check=True).stdout
        self.assertFalse(any(line.startswith("??") for line in source_status.splitlines()),
                         "Prepared pinned source contains untracked files")
        source_diff = subprocess.run(
            ["git", "diff", "--binary", "HEAD"], cwd=source, capture_output=True,
            timeout=60, check=True).stdout
        source_diff_sha = hashlib.sha256(source_diff).hexdigest()

        if explicit_output is not None:
            output.mkdir()
        else:
            output = self.new_workspace(ROOT, "action-store-catalog-")
        commands_dir = output / "commands"
        commands_dir.mkdir()

        node_version = run_command(output, "node-version", [str(node), "--version"],
                                   cwd=ROOT, env=dict(os.environ), timeout=10).stdout.strip()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        common = ["-O1", "-fexceptions", "-DTARGET_PC", "-DMELEE_WEB_ACTION_INTERVAL_TEST",
                  "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                  "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(source)]
        object_paths = []
        c_command = [sys.executable, str(compiler / "emcc.py"), *common, "-std=c11",
                     "-include", str(ROOT / "src/gameplay_compat.h"), "-c",
                     str(ROOT / "src/gameplay_action_store.c"), "-o",
                     str(output / "gameplay_action_store_c.o")]
        run_command(commands_dir, "compile-native-c", c_command, cwd=output, env=env, timeout=120)
        object_paths.append(output / "gameplay_action_store_c.o")
        cpp_inputs = [ROOT / "src" / name for name in SOURCE_NAMES]
        cpp_inputs.append(ROOT / "tests/gameplay_action_store_catalog_probe.cpp")
        for index, source_path in enumerate(cpp_inputs):
            object_path = output / f"catalog-{index:02d}-{source_path.stem}.o"
            command = [sys.executable, str(compiler / "em++.py"), *common, "-std=c++20",
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       "-c", str(source_path), "-o", str(object_path)]
            run_command(commands_dir, f"compile-cpp-{index:02d}", command,
                        cwd=output, env=env, timeout=120)
            object_paths.append(object_path)

        wasm = output / "gameplay_action_store_catalog_probe.js"
        link = [sys.executable, str(compiler / "em++.py"), "-O1", "-fexceptions",
                "-Wl,--gc-sections", *map(str, object_paths),
                "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                "-sSAFE_HEAP=1", "-sNODERAWFS=1", "-sALLOW_MEMORY_GROWTH=1",
                "-o", str(wasm)]
        run_command(commands_dir, "link-wasm", link, cwd=output, env=env, timeout=120)

        probe = run_command(output, "probe", [str(node), str(wasm), str(fighter), str(animations)],
                            cwd=output, env=env, timeout=60)
        (output / "probe.stdout.json").write_text(probe.stdout)
        data = json.loads(probe.stdout)
        self.assertEqual(data.get("schema"), "gameplay-action-store-resident-catalog-probe-v1")
        self.assertTrue(data.get("catalog_ready"))
        self.assertEqual(data.get("scope"), "base Mario ordinary authored action domain")
        self.assertEqual(data.get("fighter_kind"), 0)
        self.assertEqual(data.get("costume_index"), 0)
        self.assertEqual(data.get("model_symbol"), "PlyMario5K_Share_joint")
        self.assertEqual(data.get("row_count"), data.get("motion_count"))
        self.assertEqual(data.get("row_count"), len(data.get("rows", [])))
        self.assertEqual(data.get("nonempty_rows", 0) + data.get("empty_rows", 0),
                         data.get("row_count"))
        self.assertGreater(data.get("unique_native_keys", 0), 0)
        self.assertEqual(data.get("unique_native_keys"),
                         sum(1 for row in data["catalog_entries"] if row["archive_bytes"] > 0))
        self.assertEqual(data["nonempty_rows"], data["unique_native_keys"] + data["alias_rows"])
        self.assertEqual(data["empty_rows"], data["empty_key_groups"] + data["empty_key_alias_rows"])
        self.assertGreater(data["constructor_measurement"]["elapsed_ms"], 0)
        self.assertGreater(data["native_payload_bytes"]["native_owned_bytes"], 0)
        self.assertGreater(data["native_payload_bytes"]["retained_stream_bytes"], 0)

        source_hashes = {name: sha256(ROOT / name) for name in PINNED_SOURCE_FILES}
        tool_paths = [compiler / "emcc.py", compiler / "em++.py", compiler / "emscripten-version.txt",
                      config, sdk / "upstream/bin/clang", sdk / "upstream/bin/wasm-ld", node]
        report = {
            "schema": "gameplay-action-store-resident-catalog-evidence-v1",
            "label": "actual-source original Mario ordinary action catalog readiness and constructor setup cost",
            "scope_note": "Component readiness and setup-cost evidence only; no source/profile admission or gameplay acceptance claim.",
            "checkout": {
                "path": str(ROOT),
                "head": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                        capture_output=True, text=True, timeout=10,
                                        check=True).stdout.strip(),
                "prepared_melee_source_head": source_head,
                "prepared_melee_source_diff_sha256": source_diff_sha,
                "prepared_melee_source_status_entries": len(source_status.splitlines()),
                "source_hashes_sha256": source_hashes,
            "probe_sources_sha256": {
                name: source_hashes[name] for name in PROBE_SOURCE_FILES
            },
            "frozen_core_production_source_hashes_sha256": {
                name: source_hashes[name] for name in FROZEN_CORE_PRODUCTION_FILES
            },
            "frozen_core_control_source_hashes_current_sha256": {
                name: source_hashes[name] for name in FROZEN_CORE_CONTROL_FILES
            },
            },
            "core_freeze": {
                "path": str(core_freeze),
                "sha256": freeze_sha.lower(),
                "source_changes": expected_core,
            },
            "host": {
                "platform": platform.platform(),
                "machine": platform.machine(),
                "python": sys.version,
                "node_path": str(node),
                "node_version": node_version,
                "node_sha256": sha256(node),
                "emscripten_version": lock["emscripten"],
            },
            "tools": {str(path.relative_to(ROOT)): sha256(path) for path in tool_paths},
            "inputs": {
                "fighter_dat": {"path": str(fighter), "bytes": fighter.stat().st_size,
                                "sha256": sha256(fighter)},
                "animation_container": {"path": str(animations), "bytes": animations.stat().st_size,
                                        "sha256": sha256(animations)},
            },
            "outputs": {
                "javascript": {"path": str(wasm), "bytes": wasm.stat().st_size,
                               "sha256": sha256(wasm)},
                "wasm": {"path": str(wasm.with_suffix(".wasm")),
                         "bytes": wasm.with_suffix(".wasm").stat().st_size,
                         "sha256": sha256(wasm.with_suffix(".wasm"))},
                "probe_stdout": {"path": str(output / "probe.stdout.json"),
                                 "bytes": (output / "probe.stdout.json").stat().st_size,
                                 "sha256": sha256(output / "probe.stdout.json")},
            },
            "native_probe": data,
            "build_recipe": "Direct bounded Wasm32 Node compilation using the repository pinned Emscripten SDK and already prepared pinned Melee source; no source preparation or build-tree mutation.",
            "command_receipts": sorted(
                str(path) for path in [*commands_dir.glob("*.json"),
                                       output / "node-version.json", output / "probe.json"]),
        }
        (output / "evidence.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({
            "evidence": str(output / "evidence.json"),
            "rows": data["row_count"],
            "nonempty_rows": data["nonempty_rows"],
            "empty_rows": data["empty_rows"],
            "unique_native_keys": data["unique_native_keys"],
            "alias_rows": data["alias_rows"],
            "constructor_ms": data["constructor_measurement"]["elapsed_ms"],
            "native_payload_bytes": data["native_payload_bytes"],
        }, indent=2))


if __name__ == "__main__":
    unittest.main()
