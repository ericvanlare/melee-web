#!/usr/bin/env python3
"""Configure and build the browser integration probe using project-local tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import sqlite3

from bootstrap import read_lock, verify_sources
from gameplay_sources import prepare_sources

ROOT = Path(__file__).resolve().parents[1]

PUBLIC_RUNTIME_TARGET = "runtime-public"
PUBLIC_RUNTIME_CONFIGURATION = "Release"
PUBLIC_RUNTIME_BUILD_DIR = "build/browser-public-release"

# These are the only lifecycle executables that the content-check workflow may
# select directly.  Keep this list deliberately small and explicit: adding a
# trace requires reviewing its source contract before it becomes a build
# surface for the workflow.
TRACE_TARGETS = (
    "gameplay_content_match_trace",
    "gameplay_stage_battlefield_trace",
    "gameplay_stage_temple_trace",
    "gameplay_stage_fountain_trace",
    "gameplay_stage_old_yoshi_trace",
    "gameplay_pikachu_articles_trace",
)

# Keep the target closure in one place so callers that need to configure once
# and build several target groups can use the same reviewed target names as the
# normal CLI.  Tuples prevent accidental mutation by callers and preserve the
# existing target groups byte-for-byte.
BUILD_TARGETS = {
    "graphics": ("gx_probe",),
    "gameplay": ("gameplay_checks",),
    "runtime": ("gameplay_menu_browser",),
    PUBLIC_RUNTIME_TARGET: (PUBLIC_RUNTIME_TARGET,),
    "fighter": (
        "fighter_runtime_probe",
        "gameplay_effect_banks_trace",
        "gameplay_bonus_data_trace",
        "gameplay_stage_numeric_trace",
        "native_menu_scene_trace",
        "dat_menu_support_trace",
    ),
    "all": ("gx_probe", "gameplay_checks", "gameplay_menu_browser"),
}


def build_directory(root=ROOT, target="all", configuration="RelWithDebInfo", *,
                    pipeline_provenance=False, selective_pipelines=False):
    """Return the build directory selected by the normal build contract.

    Trace targets use the private development build directory and intentionally
    cannot be combined with either alternate graph.  Keeping this resolution
    here lets focused callers use the same path as :func:`build` without
    duplicating the SDK/build-directory rules.
    """
    root = Path(root)
    if target not in BUILD_TARGETS and target not in TRACE_TARGETS:
        raise ValueError(f"Unsupported build target: {target}")
    if target in TRACE_TARGETS and (pipeline_provenance or selective_pipelines):
        raise ValueError("trace targets require the private development build")
    if selective_pipelines:
        suffix = "-release" if configuration == "Release" else ""
        return root / ("build/browser-public-selective-release" if target == PUBLIC_RUNTIME_TARGET
                       else f"build/browser-selective{suffix}")
    if pipeline_provenance:
        return root / ("build/browser-provenance-release" if configuration == "Release"
                       else "build/browser-provenance")
    return root / (PUBLIC_RUNTIME_BUILD_DIR if target == PUBLIC_RUNTIME_TARGET else
                   ("build/browser-release" if configuration == "Release" else "build/browser"))

PUBLIC_RUNTIME_EXPORTS = (
    "_main",
    "_malloc",
    "_free",
    "_melee_web_native_menu_file",
    "_melee_web_native_menu_prepare",
    "_melee_web_native_menu_launch",
    "_melee_web_native_menu_unload",
    "_melee_web_native_menu_pause",
    "_melee_web_native_menu_message",
    "_melee_web_native_menu_running",
    "_melee_web_native_menu_phase",
    "_melee_web_native_menu_cache_idle",
    "_melee_web_input_set_activity",
    "_melee_web_input_set_keyboard",
    "_melee_web_input_set_keyboard_port",
    "_melee_web_input_set_keyboard_layout",
)
PUBLIC_RUNTIME_FORBIDDEN_EXPORTS = frozenset(
    {
        "_melee_web_native_menu_replay",
        "_melee_web_native_menu_replay_cursor",
        "_melee_web_native_menu_confirm_check",
        "_melee_web_native_menu_pad_sample",
        "_melee_web_native_menu_pad_sample_full",
        "_melee_web_native_menu_player_state",
        "_melee_web_native_menu_drive_fighter",
        "_melee_web_native_menu_drive_stage",
        "_melee_web_native_menu_stock_check",
        "_melee_web_native_menu_stock_check_ready",
        "_melee_web_native_menu_diagnostics",
        "_melee_web_native_menu_memory",
        "_melee_web_css_observe",
        "_melee_web_sss_observe",
        "_melee_web_input_message",
    }
)
PUBLIC_RUNTIME_SOURCE_FILES = (
    "CMakeLists.txt",
    "cmake/FighterRuntime.cmake",
    "patches/melee-gameplay.patch",
    "src/gameplay_menu_browser.cpp",
    "src/gameplay_menu_world.cpp",
    "src/gameplay_match_session.cpp",
    "src/gameplay_audio.c",
    "src/gameplay_audio_bank.cpp",
    "src/browser_input.cpp",
    "src/browser_input.h",
    "src/browser_controllers.cpp",
    "src/browser_controllers.h",
    "scripts/bootstrap.py",
    "scripts/build.py",
    "scripts/gameplay_bool.py",
    "scripts/gameplay_sources.py",
    "scripts/generate_common_schema.py",
    "scripts/generate_fighter_registry.py",
    "scripts/materialize_pipeline_cache.py",
    "web/initial_pipeline_cache.db.gz.b64",
    "patches/aurora-browser.patch",
    "dependencies.lock.json",
)


def _sha256(path):
    """Hash a regular local file without loading an asset into memory."""
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{path}: expected a regular file")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _file_record(path, root):
    return {
        "path": path.relative_to(root).as_posix(),
        "bytes": path.stat().st_size,
        "sha256": _sha256(path),
    }


def _tree_record(path, root):
    """Hash a deterministic path-and-bytes inventory for a source subtree."""
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"{path}: expected a regular source directory")
    digest = hashlib.sha256()
    files = 0
    for child in sorted(path.rglob("*")):
        if ".git" in child.relative_to(path).parts:
            continue
        if child.is_symlink():
            raise ValueError(f"{child}: source inventory contains a symlink")
        if not child.is_file():
            continue
        relative = child.relative_to(root).as_posix().encode("utf-8")
        digest.update(relative)
        digest.update(b"\0")
        with child.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        files += 1
    return {"path": path.relative_to(root).as_posix(), "files": files, "sha256": digest.hexdigest()}


def _prepared_source_record(root, generated):
    """Capture the pinned checkout and its exact reviewed working-tree patch."""
    if generated.is_symlink() or not generated.is_dir():
        raise ValueError(f"{generated}: prepared gameplay source must be a local directory")
    try:
        commit = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=generated, text=True
        ).strip()
        patch = generated / ".git/melee-web-composed.patch"
        applied = generated / ".git/melee-web-gameplay.patch"
        diff = subprocess.check_output(["git", "diff", "--binary", "HEAD"], cwd=generated)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"{generated}: cannot inspect prepared gameplay source: {error}") from error
    if not patch.is_file() or not applied.is_file():
        raise ValueError(f"{generated}: composed gameplay patch provenance is missing")
    return {
        "path": generated.relative_to(root).as_posix(),
        "pinned_commit": commit,
        "composed_patch": {"path": patch.relative_to(root).as_posix(), "sha256": _sha256(patch)},
        "reviewed_patch": {"path": applied.relative_to(root).as_posix(), "sha256": _sha256(applied)},
        "working_tree_diff_sha256": hashlib.sha256(diff).hexdigest(),
        "tree": _tree_record(generated, root),
    }


def _uleb(data, offset):
    value = 0
    shift = 0
    while offset < len(data):
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift > 63:
            break
    raise ValueError("malformed WebAssembly unsigned integer")


def _wasm_exports(path):
    """Return the actual WebAssembly export section, without external tools."""
    data = path.read_bytes()
    if data[:4] != b"\0asm" or data[4:8] != b"\1\0\0\0":
        raise ValueError(f"{path}: not a WebAssembly binary")
    offset = 8
    exports = []
    while offset < len(data):
        section = data[offset]
        offset += 1
        size, offset = _uleb(data, offset)
        end = offset + size
        if end > len(data):
            raise ValueError(f"{path}: truncated WebAssembly section")
        if section == 7:  # export section
            count, cursor = _uleb(data, offset)
            for _ in range(count):
                name_size, cursor = _uleb(data, cursor)
                name_end = cursor + name_size
                if name_end > end:
                    raise ValueError(f"{path}: truncated WebAssembly export name")
                name = data[cursor:name_end].decode("utf-8")
                cursor = name_end
                kind = data[cursor]
                cursor += 1
                index, cursor = _uleb(data, cursor)
                exports.append({"name": name, "kind": kind, "index": index})
            if cursor != end:
                raise ValueError(f"{path}: malformed WebAssembly export section")
        offset = end
    return tuple(exports)


def _pipeline_seed_record(root, build_dir):
    source = root / "web/initial_pipeline_cache.db.gz.b64"
    materialized = build_dir / "initial_pipeline_cache.db"
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"{source}: reviewed pipeline seed is missing")
    if materialized.is_symlink() or not materialized.is_file():
        raise ValueError(f"{materialized}: materialized pipeline seed is missing")
    # materialize_pipeline_cache.py verifies this digest before CMake can link,
    # and repeat it here so the identity record states exactly what was built.
    from materialize_pipeline_cache import EXPECTED_SHA256

    materialized_hash = _sha256(materialized)
    if materialized_hash != EXPECTED_SHA256:
        raise ValueError(f"{materialized}: pipeline seed digest mismatch: {materialized_hash}")
    tables = []
    try:
        connection = sqlite3.connect(f"file:{materialized}?mode=ro", uri=True)
        try:
            names = connection.execute(
                "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
            ).fetchall()
            for (name,) in names:
                if not name.startswith("sqlite_"):
                    count = connection.execute(
                        'SELECT COUNT(*) FROM "' + name.replace('"', '""') + '"'
                    ).fetchone()[0]
                    tables.append({"name": name, "rows": count})
        finally:
            connection.close()
    except sqlite3.Error as error:
        raise ValueError(f"{materialized}: pipeline seed SQLite inspection failed: {error}") from error
    return {
        "source": {
            "path": source.relative_to(root).as_posix(),
            "bytes": source.stat().st_size,
            "sha256": _sha256(source),
        },
        "materialized": {
            "path": materialized.relative_to(root).as_posix(),
            "bytes": materialized.stat().st_size,
            "sha256": materialized_hash,
        },
        "expected_sha256": EXPECTED_SHA256,
        "sqlite_tables": tables,
    }


def _ninja_build_block(ninja_text, output):
    """Return one exact Ninja build statement and its indented variables."""
    lines = ninja_text.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("build ") or not line.startswith(f"build {output}:"):
            continue
        block = [line]
        cursor = index + 1
        while cursor < len(lines) and (lines[cursor].startswith("  ") or not lines[cursor]):
            block.append(lines[cursor])
            cursor += 1
        return block
    raise ValueError(f"build.ninja has no statement for {output}")


def _public_audio_graph_proof(root, build_dir):
    """Capture machine-checkable proof that the public target omits GPL DSP code."""
    ninja_path = build_dir / "build.ninja"
    if ninja_path.is_symlink() or not ninja_path.is_file():
        raise ValueError(f"{ninja_path}: public build graph is missing")
    ninja_text = ninja_path.read_text(encoding="utf-8")
    target_block = _ninja_build_block(ninja_text, "gameplay_public.js")
    source_archive_block = _ninja_build_block(ninja_text, "libfighter_source_runtime_public.a")
    asset_archive_block = _ninja_build_block(ninja_text, "libfighter_asset_runtime_public.a")
    all_graph = "\n".join(target_block + source_archive_block + asset_archive_block)
    forbidden_c = "gameplay_audio_resample.c"
    forbidden_h = "gameplay_audio_resample.h"
    if forbidden_c in all_graph or forbidden_h in all_graph:
        raise ValueError("public Ninja link/archive graph references the GPL resampler")

    try:
        compile_commands = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"{build_dir}: public compile command database is unavailable") from error
    public_audio_commands = [
        entry for entry in compile_commands
        if "fighter_source_runtime_public.dir" in entry.get("command", "")
        and entry.get("file", "").endswith("/src/gameplay_audio.c")
    ]
    public_resampler_commands = [
        entry for entry in compile_commands
        if "fighter_source_runtime_public.dir" in entry.get("command", "")
        and "gameplay_audio_resample.c" in entry.get("file", "")
    ]
    if len(public_audio_commands) != 1 or public_resampler_commands:
        raise ValueError("public compile commands do not prove the intended silent audio unit")
    audio_command = public_audio_commands[0]["command"]
    if "MELEE_WEB_PUBLIC_AUDIO_DISABLED" not in audio_command:
        raise ValueError("public gameplay_audio.c compile command lacks the silent policy definition")
    depfile_rel = "CMakeFiles/fighter_source_runtime_public.dir/src/gameplay_audio.c.o.d"
    depfile = build_dir / depfile_rel
    depfile_text = ""
    if depfile.is_file() and not depfile.is_symlink():
        depfile_text = depfile.read_text(encoding="utf-8")
        if forbidden_h in depfile_text:
            raise ValueError("public gameplay_audio.c depfile references the GPL resampler header")
    elif depfile.is_symlink():
        raise ValueError(f"{depfile}: public gameplay_audio.c depfile is a symlink")

    # Ninja consumes compiler depfiles and normally removes the .d sidecar
    # after importing it into .ninja_deps.  Query that durable database instead
    # of treating a missing transient .d file as a graph failure.
    public_audio_object = "CMakeFiles/fighter_source_runtime_public.dir/src/gameplay_audio.c.o"
    ninja = root / ".venv" / "bin" / "ninja"
    if ninja.is_symlink() or not ninja.is_file():
        raise ValueError(f"{ninja}: project Ninja is unavailable for dependency proof")
    deps_result = subprocess.run(
        [str(ninja), "-t", "deps", public_audio_object],
        cwd=build_dir,
        text=True,
        capture_output=True,
        check=False,
    )
    if deps_result.returncode != 0 or not deps_result.stdout.strip():
        raise ValueError(
            f"{public_audio_object}: Ninja dependency database is unavailable: "
            f"{deps_result.stderr.strip()}"
        )
    ninja_deps_text = deps_result.stdout
    if forbidden_h in ninja_deps_text:
        raise ValueError("public gameplay_audio.c Ninja dependencies reference the GPL resampler header")

    def tokens(block):
        return [token for token in " ".join(block).split() if token.startswith("CMakeFiles/") or token.startswith("lib")]

    return {
        "schema": "melee-web-public-audio-graph-v2",
        "target": "gameplay_public",
        "excluded_inputs": ["src/gameplay_audio_resample.c", "src/gameplay_audio_resample.h"],
        "ninja": {
            "path": ninja_path.relative_to(root).as_posix(),
            "target_statement_sha256": hashlib.sha256("\n".join(target_block).encode()).hexdigest(),
            "source_archive_statement_sha256": hashlib.sha256("\n".join(source_archive_block).encode()).hexdigest(),
            "asset_archive_statement_sha256": hashlib.sha256("\n".join(asset_archive_block).encode()).hexdigest(),
            "target_inputs": tokens(target_block),
            "source_archive_inputs": tokens(source_archive_block),
            "asset_archive_inputs": tokens(asset_archive_block),
        },
        "compile_commands": {
            "path": (build_dir / "compile_commands.json").relative_to(root).as_posix(),
            "public_audio_command_sha256": hashlib.sha256(audio_command.encode()).hexdigest(),
            "public_audio_object": public_audio_object,
            "public_resampler_compile_commands": 0,
        },
        "depfile": {
            "path": (build_dir / depfile_rel).relative_to(root).as_posix(),
            "present": bool(depfile_text),
            "resampler_header_referenced": forbidden_h in depfile_text,
        },
        "ninja_deps": {
            "object": public_audio_object,
            "sha256": hashlib.sha256(ninja_deps_text.encode()).hexdigest(),
            "resampler_header_referenced": forbidden_h in ninja_deps_text,
        },
        "checks": {
            "resampler_c_in_public_ninja_graph": forbidden_c in all_graph,
            "resampler_h_in_public_ninja_graph": forbidden_h in all_graph,
            "resampler_c_in_public_compile_commands": bool(public_resampler_commands),
            "resampler_h_in_public_depfile": forbidden_h in depfile_text,
            "resampler_h_in_public_ninja_deps": forbidden_h in ninja_deps_text,
        },
    }


def _source_inputs_record(root, gameplay_source):
    """Return the complete native-input fingerprint used by the producer."""
    root_files = {
        path: _sha256(root / path)
        for path in PUBLIC_RUNTIME_SOURCE_FILES
        if path not in {"CMakeLists.txt", "cmake/FighterRuntime.cmake"}
    }
    root_files["CMakeLists.txt"] = _sha256(root / "CMakeLists.txt")
    root_files["cmake/FighterRuntime.cmake"] = _sha256(root / "cmake/FighterRuntime.cmake")
    for path in (
        "tests/native_menu_alarm_unavailable.c",
        "tests/native_menu_fighter_input.c",
        "tests/native_menu_stage_input.c",
    ):
        root_files[path] = _sha256(root / path)
    return {
        "files_sha256": dict(sorted(root_files.items())),
        "trees": {
            "src": _tree_record(root / "src", root),
            "cmake": _tree_record(root / "cmake", root),
        },
        "prepared_gameplay": _prepared_source_record(root, gameplay_source.parent),
    }


def _verify_public_exports(wasm, javascript):
    exports = _wasm_exports(wasm)
    functions = tuple(item["name"] for item in exports if item["kind"] == 0)
    function_set = set(functions)
    # Release Emscripten minifies Wasm export names. Its generated JS contains
    # the authoritative public-name -> Wasm-name assignments, which we verify
    # against the binary rather than assuming the names survive minification.
    source = javascript.read_text(encoding="utf-8")
    bindings = {}
    for name in PUBLIC_RUNTIME_EXPORTS:
        escaped = re.escape(name)
        match = re.search(
            rf"{escaped}=Module\[\"{escaped}\"\]=wasmExports\[\"([^\"]+)\"\]",
            source,
        )
        if match:
            bindings[name] = match.group(1)
    missing = [name for name in PUBLIC_RUNTIME_EXPORTS if name not in bindings]
    absent_binary = sorted(name for name in bindings.values() if name not in function_set)
    forbidden = sorted(
        name for name in PUBLIC_RUNTIME_FORBIDDEN_EXPORTS
        if re.search(rf"{re.escape(name)}=Module\[", source)
    )
    if missing:
        raise ValueError(f"{javascript}: missing public runtime JS bindings: {', '.join(missing)}")
    if absent_binary:
        raise ValueError(f"{wasm}: JS bindings absent from Wasm export section: {', '.join(absent_binary)}")
    if forbidden:
        raise ValueError(f"{javascript}: forbidden public runtime bindings: {', '.join(forbidden)}")
    return {
        "required": list(PUBLIC_RUNTIME_EXPORTS),
        "functions": list(functions),
        "all": list(exports),
        "javascript_bindings": bindings,
        "forbidden_absent": sorted(PUBLIC_RUNTIME_FORBIDDEN_EXPORTS),
    }


def _write_public_identity(root, build_dir, version, cmake, ninja, gameplay_source,
                           expected_source_inputs=None):
    artifact_names = ("gameplay_public.js", "gameplay_public.wasm", "gameplay_public.data")
    artifacts = []
    for name in artifact_names:
        path = build_dir / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"{path}: public runtime artifact is missing or a symlink")
        artifacts.append(_file_record(path, root))
    wasm = build_dir / "gameplay_public.wasm"
    exports = _verify_public_exports(wasm, build_dir / "gameplay_public.js")
    source_inputs = _source_inputs_record(root, gameplay_source)
    if expected_source_inputs is not None and source_inputs != expected_source_inputs:
        raise ValueError("native source inputs changed during the public runtime build")
    emscripten_version = root / ".deps/emsdk/upstream/emscripten/emscripten-version.txt"
    emscripten_config = root / ".deps/emsdk/.emscripten"
    emcc = root / ".deps/emsdk/upstream/emscripten/emcc"
    tool_paths = (emscripten_version, emscripten_config, emcc, cmake, ninja)
    tool_hashes = {
        path.relative_to(root).as_posix(): _sha256(path)
        for path in tool_paths
    }
    identity = {
        "schema": "melee-web-runtime-public-build-v2",
        "target": "runtime-public",
        "configuration": "Release",
        "artifact_root": build_dir.relative_to(root).as_posix(),
        "artifacts": artifacts,
        "wasm_exports": exports,
        "audio_policy": {
            "mode": "disabled",
            "pcm_output": False,
            "dsp_resampler": False,
            "dsp_coefficients_required": False,
        },
        "audio_graph": _public_audio_graph_proof(root, build_dir),
        "source_inputs": source_inputs,
        "toolchain": {
            "emscripten": version,
            "sha256": tool_hashes,
            "cmake": subprocess.check_output([str(cmake), "--version"], text=True).splitlines()[0],
            "ninja": subprocess.check_output([str(ninja), "--version"], text=True).strip(),
        },
        "pipeline_seed": _pipeline_seed_record(root, build_dir),
        "upload_convention": {
            "group": "artifacts plus the reviewed public-shell files",
            "identity_path": "build/runtime-public-identity.json",
            "identity_is_outside_artifact_root": True,
        },
    }
    identity_path = root / "build/runtime-public-identity.json"
    if identity_path.is_symlink():
        raise ValueError(f"{identity_path}: identity sidecar must not be a symlink")
    identity_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = identity_path.with_name(identity_path.name + ".tmp")
    if temporary.is_symlink():
        raise ValueError(f"{temporary}: refusing a symlink identity temporary")
    temporary.write_text(json.dumps(identity, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(identity_path)
    return identity_path


def build(jobs, root=ROOT, target="all", configuration="RelWithDebInfo", *,
          pipeline_provenance=False, selective_pipelines=False, configure_only=False,
          trace_targets=None):
    trace_targets = tuple(trace_targets or ())
    if trace_targets:
        if target != "all":
            raise ValueError("--target and --trace-target are mutually exclusive")
        unknown = [name for name in trace_targets if name not in TRACE_TARGETS]
        if unknown:
            raise ValueError(f"Unsupported trace target: {unknown[0]}")
        if pipeline_provenance or selective_pipelines:
            raise ValueError("trace targets require the private development build")
    if configure_only and target == PUBLIC_RUNTIME_TARGET:
        # The public target writes an identity sidecar only after a complete
        # build.  A configure-only invocation must not leave an apparently
        # usable public identity next to stale artifacts.
        raise ValueError("--configure-only cannot be used with runtime-public")
    if selective_pipelines and (target not in {"runtime", PUBLIC_RUNTIME_TARGET} or pipeline_provenance):
        raise ValueError("--selective-pipelines requires runtime/runtime-public without --pipeline-provenance")
    if pipeline_provenance and target != "runtime":
        raise ValueError("--pipeline-provenance requires the private runtime target")
    if target == PUBLIC_RUNTIME_TARGET and configuration != PUBLIC_RUNTIME_CONFIGURATION:
        raise ValueError("runtime-public is Release-only; pass --configuration Release")
    lock = read_lock(root)
    verify_sources(root, lock)
    # Registry strings/counts are generated from the pinned source, not game
    # bytes. Fail on source drift before compiling an outdated binding table.
    subprocess.run([sys.executable, str(root / "scripts/generate_fighter_registry.py"), "--check"],
                   cwd=root, check=True)
    gameplay_source = prepare_sources(root, lock)
    subprocess.run([sys.executable, str(root / "scripts/generate_common_schema.py"), "--check"],
                   cwd=root, check=True)
    if (root / ".venv").is_symlink():
        raise ValueError(".venv must be a local directory, not a symlink")
    bins = root / ".venv" / ("Scripts" if os.name == "nt" else "bin")
    cmake = bins / ("cmake.exe" if os.name == "nt" else "cmake")
    ninja = bins / ("ninja.exe" if os.name == "nt" else "ninja")
    sdk = root / ".deps/emsdk"
    emscripten = sdk / "upstream/emscripten"
    emcmake = emscripten / ("emcmake.bat" if os.name == "nt" else "emcmake")
    if not all(path.is_file() for path in (cmake, ninja, emcmake, sdk / ".emscripten")):
        raise ValueError("Build tools are missing. Run python3 scripts/bootstrap.py first.")
    version = (emscripten / "emscripten-version.txt").read_text().strip().strip('"')
    if version != lock["emscripten"]:
        raise ValueError(f"Expected Emscripten {lock['emscripten']}, got {version}. Run bootstrap.py.")
    env = dict(os.environ)
    env["PATH"] = str(bins) + os.pathsep + env.get("PATH", "")
    # A separately activated SDK must not redirect this build to global tools/cache.
    env["EMSDK"] = str(sdk)
    env["EM_CONFIG"] = str(sdk / ".emscripten")
    env["EM_CACHE"] = str(emscripten / "cache")
    env["EMSDK_PYTHON"] = sys.executable
    build_target = trace_targets[0] if trace_targets else target
    build_dir = build_directory(root, target=build_target, configuration=configuration,
                                pipeline_provenance=pipeline_provenance,
                                selective_pipelines=selective_pipelines)
    if (root / "build").is_symlink() or build_dir.is_symlink():
        raise ValueError("Build output must be a local directory, not a symlink")
    source_inputs_before = (
        _source_inputs_record(root, gameplay_source)
        if target == PUBLIC_RUNTIME_TARGET else None
    )
    configure = [str(emcmake), str(cmake), "-S", str(root), "-B", str(build_dir),
                 "-G", "Ninja", f"-DCMAKE_BUILD_TYPE={configuration}",
                 f"-DMELEE_WEB_GAMEPLAY_SOURCE_DIR={gameplay_source}",
                 f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    configure.append(
        f"-DMELEE_WEB_PUBLIC_RUNTIME={'ON' if target == PUBLIC_RUNTIME_TARGET else 'OFF'}"
    )
    configure.append(f"-DMELEE_WEB_PIPELINE_PROVENANCE={'ON' if pipeline_provenance else 'OFF'}")
    configure.append(f"-DMELEE_WEB_SELECTIVE_PIPELINES={'ON' if selective_pipelines else 'OFF'}")
    subprocess.run(configure, cwd=root, env=env, check=True)
    if configure_only:
        return
    targets = trace_targets if trace_targets else BUILD_TARGETS[target]
    subprocess.run([str(cmake), "--build", str(build_dir), "--target", *targets, "-j", str(jobs)],
                   cwd=root, env=env, check=True)
    if target == PUBLIC_RUNTIME_TARGET:
        if _source_inputs_record(root, gameplay_source) != source_inputs_before:
            raise ValueError("native source inputs changed during the public runtime build")
        identity_path = _write_public_identity(
            root, build_dir, version, cmake, ninja, gameplay_source,
            expected_source_inputs=source_inputs_before,
        )
        print(f"Wrote {identity_path.relative_to(root)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 6))
    parser.add_argument("--target", choices=("graphics", "gameplay", "fighter", "runtime", PUBLIC_RUNTIME_TARGET, "all"))
    parser.add_argument("--trace-target", dest="trace_targets", choices=TRACE_TARGETS,
                        action="append",
                        help="Build one reviewed lifecycle trace; repeat for multiple traces")
    parser.add_argument("--configuration", choices=("RelWithDebInfo", "Release"), default="RelWithDebInfo")
    parser.add_argument("--pipeline-provenance", action="store_true",
                        help="Compile the private runtime recorder into a separate build directory")
    parser.add_argument("--selective-pipelines", action="store_true",
                        help="Prepare certified upcoming pipeline unions in a separate runtime build")
    parser.add_argument("--configure-only", action="store_true",
                        help="Configure the selected build directory without compiling targets")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.target is not None and args.trace_targets:
        parser.error("--target and --trace-target are mutually exclusive")
    try:
        build(args.jobs, target=args.target or "all", configuration=args.configuration,
              pipeline_provenance=args.pipeline_provenance, selective_pipelines=args.selective_pipelines,
              configure_only=args.configure_only, trace_targets=args.trace_targets)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"build: {error}") from error


if __name__ == "__main__":
    main()
